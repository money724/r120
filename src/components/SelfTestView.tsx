import React, { useEffect, useState } from 'react';
import { R120SigStatus, runSignalSelfTest } from '../r120/r120Signal';
import { R120CztStatus, runCztSelfTest } from '../r120/r120Czt';
import { R120RangeStatus, runRangeSelfTest } from '../r120/r120Range';
import { R120PhaseStatus, runPhaseSelfTest } from '../r120/r120Phase';
import { R120MeasStatus, runMeasureSelfTest } from '../r120/r120Measure';
import { Play } from 'lucide-react';

interface SelfTestRow {
  id: string;
  moduleFile: string;
  fnName: string;
  passed: boolean;
  measured: string;
  assertion: string;
  durationMs: number;
}

export const SelfTestView: React.FC = () => {
  const [rows, setRows] = useState<SelfTestRow[]>([]);

  const executeAll = () => {
    const results: SelfTestRow[] = [];

    // 1. r120_signal.c
    const t0 = performance.now();
    const sig = runSignalSelfTest();
    const dt0 = performance.now() - t0;
    results.push({
      id: 'sig',
      moduleFile: 'User/r120_signal.c',
      fnName: 'R120_SIG_RunSelfTest',
      passed: sig.status === R120SigStatus.Ok && Math.abs(sig.result.signedBeatHz - 27777.7778) < 25,
      measured: `f_beat = ${sig.result.signedBeatHz.toFixed(2)} Hz · Contrast ${sig.result.peakContrastDb.toFixed(1)} dB · Amp ${sig.result.amplitudeCounts.toFixed(1)}`,
      assertion: 'Expected ~27,777.78 Hz, contrast ≥ 18 dB, amp ≥ 5 counts',
      durationMs: dt0,
    });

    // 2. r120_czt.c
    const t1 = performance.now();
    const czt = runCztSelfTest();
    const dt1 = performance.now() - t1;
    results.push({
      id: 'czt',
      moduleFile: 'User/r120_czt.c',
      fnName: 'R120_CZT_RunSelfTest',
      passed: czt.status === R120CztStatus.Ok && Math.abs(czt.result.refinedSignedBeatHz - 27777.7778) < 2.0,
      measured: `f_czt = ${czt.result.refinedSignedBeatHz.toFixed(3)} Hz · Δ = ${(czt.result.refinedSignedBeatHz - 27777.7778).toFixed(3)} Hz`,
      assertion: 'Expected 27,777.78 Hz ± 2.0 Hz (129-point direct CZT)',
      durationMs: dt1,
    });

    // 3. r120_range.c
    const t2 = performance.now();
    const rng = runRangeSelfTest();
    const dt2 = performance.now() - t2;
    results.push({
      id: 'range',
      moduleFile: 'User/r120_range.c',
      fnName: 'R120_RANGE_RunSelfTest',
      passed: rng.status === R120RangeStatus.Ok,
      measured: `R = ${rng.result.distanceM.toFixed(5)} m · Slope = ${(rng.result.slopeHzPerS / 1e12).toFixed(4)} THz/s`,
      assertion: '|R - 4.99654 m| ≤ 0.020 m (5 GHz / 6 ms chirp)',
      durationMs: dt2,
    });

    // 4. r120_phase.c
    const t3 = performance.now();
    const ph = runPhaseSelfTest();
    const dt3 = performance.now() - t3;
    results.push({
      id: 'phase',
      moduleFile: 'User/r120_phase.c',
      fnName: 'R120_PHASE_RunSelfTest',
      passed: ph.status === R120PhaseStatus.Ok,
      measured: `φ = ${ph.result.phaseRad.toFixed(4)} rad (err ${ph.phaseErrorRad.toFixed(4)}) · Coherent ${(ph.result.coherentFraction * 100).toFixed(2)}%`,
      assertion: '|Δφ| ≤ 0.030 rad around 0.7000 rad · Coherent ≥ 95%',
      durationMs: dt3,
    });

    // 5. r120_measure.c
    const t4 = performance.now();
    const meas = runMeasureSelfTest();
    const dt4 = performance.now() - t4;
    results.push({
      id: 'measure',
      moduleFile: 'User/r120_measure.c',
      fnName: 'R120_MEAS_RunSelfTest',
      passed: meas.status === R120MeasStatus.Ok,
      measured:
        meas.range && meas.phase
          ? `R = ${meas.range.distanceM.toFixed(5)} m · φ = ${meas.phase.phaseRad.toFixed(4)} rad · Coherent ${(meas.phase.coherentFraction * 100).toFixed(2)}%`
          : `Status code ${meas.status}`,
      assertion: '|R - 5.000 m| ≤ 0.015 m · |Δφ| ≤ 0.050 rad · Coherent ≥ 90%',
      durationMs: dt4,
    });

    setRows(results);
  };

  useEffect(() => {
    executeAll();
  }, []);

  const passCount = rows.filter((r) => r.passed).length;

  return (
    <div className="bg-white border border-slate-200 rounded-lg overflow-hidden">
      <div className="flex flex-wrap items-center justify-between gap-4 px-5 py-4 border-b border-slate-200 bg-slate-50/70">
        <div>
          <h2 className="text-base font-semibold text-slate-900">Firmware CPU Self-Test Verification Suite</h2>
          <p className="text-xs text-slate-500 mt-0.5">
            Executes all five built-in synthetic I/Q self-tests ported verbatim from the STM32F767 DSP modules.
          </p>
        </div>
        <div className="flex items-center gap-4">
          <span className="text-xs font-mono tabular-nums font-semibold text-emerald-700">
            ● {passCount} / {rows.length} Passed
          </span>
          <button
            type="button"
            onClick={executeAll}
            className="inline-flex items-center gap-1.5 px-3.5 py-2 text-xs font-medium text-white bg-blue-600 rounded-md hover:bg-blue-700 whitespace-nowrap transition-colors"
          >
            <Play className="w-3.5 h-3.5" />
            Re-run All Tests
          </button>
        </div>
      </div>

      <div className="overflow-x-auto">
        <table className="w-full text-left border-collapse text-xs">
          <thead>
            <tr className="border-b border-slate-200 bg-slate-50/40 text-slate-500">
              <th className="py-2.5 px-5 font-medium">Result</th>
              <th className="py-2.5 px-4 font-medium">Firmware Function</th>
              <th className="py-2.5 px-4 font-medium">Measured Output</th>
              <th className="py-2.5 px-4 font-medium">Pass Tolerance</th>
              <th className="py-2.5 px-5 font-medium text-right">Runtime</th>
            </tr>
          </thead>
          <tbody className="divide-y divide-slate-100">
            {rows.map((row) => (
              <tr key={row.id} className="hover:bg-slate-50/70">
                <td className="py-3 px-5 font-mono font-semibold whitespace-nowrap">
                  <span className={row.passed ? 'text-emerald-700' : 'text-rose-700'}>
                    {row.passed ? '● PASS' : '✖ FAIL'}
                  </span>
                </td>
                <td className="py-3 px-4">
                  <div className="font-mono font-medium text-slate-900">{row.fnName}</div>
                  <div className="text-[11px] font-mono text-slate-400">{row.moduleFile}</div>
                </td>
                <td className="py-3 px-4 font-mono tabular-nums text-slate-700">{row.measured}</td>
                <td className="py-3 px-4 text-slate-600">{row.assertion}</td>
                <td className="py-3 px-5 font-mono tabular-nums text-right text-slate-500 whitespace-nowrap">
                  {row.durationMs.toFixed(2)} ms
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
};
