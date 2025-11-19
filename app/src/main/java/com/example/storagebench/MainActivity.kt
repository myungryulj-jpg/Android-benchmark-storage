
package com.example.storagebench

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.example.storagebench.databinding.ActivityMainBinding
import com.example.storagebench.model.IoConfig
import com.example.storagebench.native.NativeBridge
import com.github.mikephil.charting.components.Legend
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import kotlinx.coroutines.*
import java.io.File

class MainActivity : AppCompatActivity() {
    private lateinit var b: ActivityMainBinding
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        b = ActivityMainBinding.inflate(layoutInflater)
        setContentView(b.root)

        val types = arrayOf("SEQ_READ", "SEQ_WRITE", "RAND_READ", "RAND_WRITE")
        b.spinnerTestType.adapter = android.widget.ArrayAdapter(this, android.R.layout.simple_list_item_1, types)

        setupChart()
        b.btnRun.setOnClickListener { runTriple() }
    }

    private fun setupChart() {
        b.lineChart.description.isEnabled = false
        b.lineChart.legend.verticalAlignment = Legend.LegendVerticalAlignment.BOTTOM
        b.lineChart.legend.horizontalAlignment = Legend.LegendHorizontalAlignment.RIGHT
        b.lineChart.setTouchEnabled(true)
        b.lineChart.setPinchZoom(true)
    }

    private fun runTriple() {
        val dir = getExternalFilesDir(null) ?: filesDir
        val testFile = File(dir, "bench_test.bin").absolutePath

        val cfg = IoConfig(
            path = testFile,
            testType = b.spinnerTestType.selectedItem.toString(),
            fileSizeBytes = (b.editFileMB.text.toString().ifBlank { "2048" }.toLong()) * 1024L * 1024L,
            blockSizeBytes = (b.editBlockKB.text.toString().ifBlank { "128" }.toInt()) * 1024,
            qd = b.editQD.text.toString().ifBlank { "4" }.toInt(),
            durationSec = b.editDurationSec.text.toString().ifBlank { "20" }.toInt(),
            warmupSec = b.editWarmupSec.text.toString().ifBlank { "3" }.toInt(),
            useDirect = b.checkUseDirect.isChecked
        )

        scope.launch {
            val datasets = mutableListOf<LineDataSet>()
            val summaries = StringBuilder()
            b.textSummary.text = "Running..."

            repeat(3) { idx ->
                val res = withContext(Dispatchers.Default) { NativeBridge.runBenchmark(cfg) }
                val entries = res.series.map { Entry(it.second.toFloat(), it.mbps.toFloat()) }
                val ds = LineDataSet(entries, "Run ${idx + 1}")
                ds.lineWidth = 2f
                ds.setDrawCircles(false)
                ds.setDrawValues(false)
                datasets += ds

                summaries.append("Run ${idx + 1}: ${"%.1f".format(res.mbps)} MB/s, QD=${cfg.qd}, blk=${cfg.blockSizeBytes/1024}KB\n")

                b.lineChart.data = LineData(datasets as List<LineDataSet>?)
                b.lineChart.invalidate()
            }
            b.textSummary.text = summaries.toString()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        scope.cancel()
    }
}
