
package com.example.storagebench.native
import com.example.storagebench.model.IoConfig
import com.example.storagebench.model.IoResult

object NativeBridge {
    init { System.loadLibrary("iobench") }
    external fun runBenchmark(cfg: IoConfig): IoResult
}
