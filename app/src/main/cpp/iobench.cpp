
#include <jni.h>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <random>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <algorithm>

static inline uint64_t now_us(){ timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000000ull + ts.tv_nsec/1000ull; }

struct Cfg { std::string path; std::string type; long long fileSize; int block; int qd; int dur; int warm; bool useDirect; };
struct Point { int sec; double mbps; };

static int open_file(const char* p, bool wr, bool tryDirect, long long size){
    int flags = wr? (O_RDWR|O_CREAT) : O_RDONLY;
#ifdef O_DIRECT
    if(tryDirect) flags |= O_DIRECT;
#endif
    int fd = open(p, flags, 0664);
#ifdef O_DIRECT
    if(fd<0 && tryDirect){ flags &= ~O_DIRECT; fd = open(p, flags, 0664);} 
#endif
    if(fd>=0 && wr){ posix_fallocate(fd,0,size); }
    return fd;
}

extern "C" JNIEXPORT jobject JNICALL
Java_com_example_storagebench_native_NativeBridge_runBenchmark(
        JNIEnv* env, jobject /*thiz*/, jobject jcfg){
    // read cfg
    jclass C = env->GetObjectClass(jcfg);
    auto S  = [&](const char* n){ jfieldID f=env->GetFieldID(C,n,"Ljava/lang/String;"); jstring js=(jstring)env->GetObjectField(jcfg,f); const char* cs=env->GetStringUTFChars(js,0); std::string v(cs); env->ReleaseStringUTFChars(js,cs); return v; };
    auto I  = [&](const char* n){ jfieldID f=env->GetFieldID(C,n,"I"); return env->GetIntField(jcfg,f); };
    auto J  = [&](const char* n){ jfieldID f=env->GetFieldID(C,n,"J"); return env->GetLongField(jcfg,f); };
    auto Z  = [&](const char* n){ jfieldID f=env->GetFieldID(C,n,"Z"); return env->GetBooleanField(jcfg,f)==JNI_TRUE; };

    Cfg cfg{ S("path"), S("testType"), (long long)J("fileSizeBytes"), I("blockSizeBytes"), I("qd"), I("durationSec"), I("warmupSec"), Z("useDirect") };

    bool isWrite = (cfg.type=="SEQ_WRITE"||cfg.type=="RAND_WRITE");
    bool isRand  = (cfg.type=="RAND_WRITE"||cfg.type=="RAND_READ");

    int fd = open_file(cfg.path.c_str(), isWrite, cfg.useDirect, cfg.fileSize);
    if(fd<0){ jclass ex=env->FindClass("java/io/IOException"); env->ThrowNew(ex,"open failed"); return nullptr; }

    std::atomic<bool> stop(false);
    std::atomic<long long> bytes(0); std::atomic<long long> ops(0);

    size_t blk = (size_t)cfg.block;
    posix_fallocate(fd,0,cfg.fileSize);

    auto worker = [&](int tid){
        void* buf=nullptr; posix_memalign(&buf,4096, blk);
        if(isWrite){ for(size_t i=0;i<blk;i+=4) ((uint32_t*)buf)[i/4]= (uint32_t)(i*2654435761u); }
        std::mt19937_64 rng((uint64_t)tid*88172645463393265ull);
        std::uniform_int_distribution<long long> d(0,(cfg.fileSize - blk)/blk);
        long long seqOff=0;
        while(!stop.load(std::memory_order_relaxed)){
            long long off = isRand ? d(rng)*blk : seqOff;
            if(!isRand){ seqOff += blk; if(seqOff+blk>cfg.fileSize) seqOff=0; }
            ssize_t n = isWrite ? pwrite(fd, buf, blk, off) : pread(fd, buf, blk, off);
            if(n==(ssize_t)blk){ bytes.fetch_add(n,std::memory_order_relaxed); ops.fetch_add(1,std::memory_order_relaxed); } else break;
        }
        free(buf);
    };

    // warmup
    std::vector<std::thread> ths; ths.reserve(cfg.qd);
    for(int i=0;i<cfg.qd;i++) ths.emplace_back(worker,i+1);
    auto warmEnd = std::chrono::steady_clock::now() + std::chrono::seconds(std::max(0,cfg.warm));
    while(std::chrono::steady_clock::now()<warmEnd) std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // measurement 1s sampling
    std::vector<Point> series; series.reserve(cfg.dur+1);
    long long prevB=0; auto end = std::chrono::steady_clock::now()+std::chrono::seconds(cfg.dur);
    int sec=0; 
    while(std::chrono::steady_clock::now()<end){
        std::this_thread::sleep_for(std::chrono::seconds(1));
        long long cur=bytes.load(); long long delta = cur - prevB; prevB = cur;
        double mbps = (double)delta / (1024.0*1024.0);
        series.push_back({sec++, mbps});
    }
    stop.store(true);
    for(auto& t: ths) t.join();

    if(isWrite) fdatasync(fd);
    close(fd);

    double totalMB = (double)bytes.load()/(1024.0*1024.0);
    double mbpsAvg = (cfg.dur>0)? totalMB / (double)cfg.dur : 0.0;
    double iops = (cfg.dur>0)? (double)ops.load() / (double)cfg.dur : 0.0;

    // Build Kotlin objects
    jclass PCls = env->FindClass("com/example/storagebench/model/IoPoint");
    jmethodID Pcons = env->GetMethodID(PCls, "<init>", "(ID)V");

    jclass ArrayList = env->FindClass("java/util/ArrayList");
    jmethodID ALinit = env->GetMethodID(ArrayList, "<init>", "()V");
    jmethodID ALadd  = env->GetMethodID(ArrayList, "add", "(Ljava/lang/Object;)Z");
    jobject list = env->NewObject(ArrayList, ALinit);
    for(const auto& pt: series){
        jobject pobj = env->NewObject(PCls, Pcons, (jint)pt.sec, (jdouble)pt.mbps);
        env->CallBooleanMethod(list, ALadd, pobj);
        env->DeleteLocalRef(pobj);
    }

    jclass RCls = env->FindClass("com/example/storagebench/model/IoResult");
    jmethodID Rcons = env->GetMethodID(RCls, "<init>", "(DDDDJJLjava/lang/String;Ljava/util/List;Ljava/lang/String;)V");
    jstring jengine = env->NewStringUTF(cfg.useDirect? "O_DIRECT/Maybe" : "BUFFERED");
    jstring jnote = env->NewStringUTF("");

    jobject R = env->NewObject(RCls, Rcons,
        (jdouble)mbpsAvg,
        (jdouble)iops,
        (jdouble)0.0,
        (jdouble)0.0,
        (jdouble)0.0,
        (jlong)bytes.load(),
        (jlong)ops.load(),
        jengine,
        list,
        jnote);

    return R;
}
