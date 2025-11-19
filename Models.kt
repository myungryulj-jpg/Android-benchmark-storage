
package com.example.storagebench.model
import android.os.Parcelable
import kotlinx.parcelize.Parcelize

@Parcelize
data class IoConfig(
    val path: String,
    val testType: String, // SEQ_READ, SEQ_WRITE, RAND_READ, RAND_WRITE
    val fileSizeBytes: Long,
    val blockSizeBytes: Int,
    val qd: Int,
    val durationSec: Int,
    val warmupSec: Int,
    val useDirect: Boolean
) : Parcelable

@Parcelize
data class IoPoint(
    val second: Int,
    val mbps: Double
) : Parcelable

@Parcelize
data class IoResult(
    val mbps: Double,
    val iops: Double,
    val avgLatencyUs: Double,
    val p99LatencyUs: Double,
    val maxLatencyUs: Double,
    val bytesDone: Long,
    val opsDone: Long,
    val engine: String,
    val series: List<IoPoint>,
    val note: String? = null
) : Parcelable
