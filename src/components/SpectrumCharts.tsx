import React, { useMemo, useState } from 'react';
import {
  R120_RANGE_LIGHT_SPEED_MPS,
  R120RangeConfig,
} from '../r120/r120Range';
import { R120MeasFullResult } from '../r120/r120Measure';
import { R120_SIG_MAX_BEAT_HZ, R120_SIG_MIN_BEAT_HZ } from '../r120/r120Signal';

interface SpectrumChartsProps {
  packed: Uint32Array;
  sampleRateHz: number;
  rangeConfig: R120RangeConfig;
  result: R120MeasFullResult;
  expectedTargetBeatHz: number;
}

type PlotTab = 'iq' | 'fft' | 'czt';

function beatHzToDistanceM(beatHz: number, rangeCfg: R120RangeConfig): number {
  const slope = (rangeCfg.bandwidthHz * 1_000_000.0) / rangeCfg.durationUs;
  if (slope <= 0) return 0;
  return (R120_RANGE_LIGHT_SPEED_MPS * Math.abs(beatHz)) / (2.0 * slope) + rangeCfg.distanceOffsetM;
}

export const SpectrumCharts: React.FC<SpectrumChartsProps> = ({
  packed,
  sampleRateHz,
  rangeConfig,
  result,
  expectedTargetBeatHz,
}) => {
  // Default directly to live Dual-ADC I/Q oscilloscope for real-time AD monitoring
  const [activePlot, setActivePlot] = useState<PlotTab>('iq');
  const [fftSpanMode, setFftSpanMode] = useState<'full' | 'activeSide'>('activeSide');
  const [iqWindowStart, setIqWindowStart] = useState<number>(0);
  const [iqWindowSpan, setIqWindowSpan] = useState<number>(180);
  const [hoverInfo, setHoverInfo] = useState<{ x: number; label: string } | null>(null);

  const width = 880;
  const height = 270;
  const padL = 58;
  const padR = 20;
  const padT = 22;
  const padB = 34;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  const fftData = useMemo(() => {
    const fft = result.fft;
    if (!fft) return null;
    const freqs = fft.shiftedFreqHz;
    const powers = fft.shiftedPowerDb;
    const total = freqs.length;

    const peakSign = fft.result.signedBeatHz < 0 ? -1 : 1;
    let startIdx = 0;
    let endIdx = total - 1;

    if (fftSpanMode === 'activeSide') {
      const targetMaxHz = Math.min(
        sampleRateHz * 0.5,
        Math.max(80_000, Math.abs(expectedTargetBeatHz) * 2.2)
      );
      const fMin = peakSign > 0 ? 0 : -targetMaxHz;
      const fMax = peakSign > 0 ? targetMaxHz : 0;
      startIdx = Math.max(0, Math.floor(((fMin + sampleRateHz / 2) / sampleRateHz) * total));
      endIdx = Math.min(total - 1, Math.ceil(((fMax + sampleRateHz / 2) / sampleRateHz) * total));
    }

    const fStart = freqs[startIdx];
    const fEnd = freqs[endIdx];
    const dbMin = -60;
    const dbMax = 65;

    const buckets = Math.min(440, Math.max(1, endIdx - startIdx + 1));
    const step = (endIdx - startIdx + 1) / buckets;
    const pts: string[] = [];

    for (let b = 0; b < buckets; ++b) {
      const i0 = Math.floor(startIdx + b * step);
      const i1 = Math.min(endIdx, Math.max(i0, Math.floor(startIdx + (b + 1) * step) - 1));
      let bestDb = -Infinity;
      let bestFreq = freqs[i0];
      for (let i = i0; i <= i1; ++i) {
        if (powers[i] > bestDb) {
          bestDb = powers[i];
          bestFreq = freqs[i];
        }
      }
      const x = padL + ((bestFreq - fStart) / Math.max(1, fEnd - fStart)) * plotW;
      const clampedDb = Math.max(dbMin, Math.min(dbMax, bestDb));
      const y = padT + ((dbMax - clampedDb) / (dbMax - dbMin)) * plotH;
      pts.push(`${x.toFixed(1)},${y.toFixed(1)}`);
    }

    return {
      polyline: pts.join(' '),
      fStart,
      fEnd,
      dbMin,
      dbMax,
      freqs,
      powers,
      bgDb: fft.backgroundMeanDb,
    };
  }, [result.fft, fftSpanMode, sampleRateHz, expectedTargetBeatHz, plotW, plotH]);

  const cztData = useMemo(() => {
    const czt = result.czt;
    if (!czt || czt.gridFreqHz.length === 0) return null;
    const fStart = czt.result.gridStartHz;
    const fEnd = czt.result.gridEndHz;
    let maxDb = -Infinity;
    let minDb = Infinity;
    for (let i = 0; i < czt.gridPowerDb.length; ++i) {
      if (czt.gridPowerDb[i] > maxDb) maxDb = czt.gridPowerDb[i];
      if (czt.gridPowerDb[i] < minDb) minDb = czt.gridPowerDb[i];
    }
    const dbTop = Math.ceil((maxDb + 4) / 5) * 5;
    const dbBot = Math.max(dbTop - 45, Math.floor((minDb - 2) / 5) * 5);

    const pts: string[] = [];
    for (let i = 0; i < czt.gridFreqHz.length; ++i) {
      const x = padL + ((czt.gridFreqHz[i] - fStart) / Math.max(1e-6, fEnd - fStart)) * plotW;
      const clamped = Math.max(dbBot, Math.min(dbTop, czt.gridPowerDb[i]));
      const y = padT + ((dbTop - clamped) / Math.max(1, dbTop - dbBot)) * plotH;
      pts.push(`${x.toFixed(1)},${y.toFixed(1)}`);
    }
    return {
      polyline: pts.join(' '),
      fStart,
      fEnd,
      dbTop,
      dbBot,
      freqs: czt.gridFreqHz,
      powers: czt.gridPowerDb,
    };
  }, [result.czt, plotW, plotH]);

  const iqData = useMemo(() => {
    const count = packed.length;
    const windowLen = Math.min(iqWindowSpan, count);
    const start = Math.max(0, Math.min(count - windowLen, iqWindowStart));
    const iPts: string[] = [];
    const qPts: string[] = [];

    for (let k = 0; k < windowLen; ++k) {
      const idx = start + k;
      const word = packed[idx];
      const iVal = word & 0x0fff;
      const qVal = (word >>> 16) & 0x0fff;
      const x = padL + (k / Math.max(1, windowLen - 1)) * plotW;
      const yi = padT + ((4095 - iVal) / 4095) * plotH;
      const yq = padT + ((4095 - qVal) / 4095) * plotH;
      iPts.push(`${x.toFixed(1)},${yi.toFixed(1)}`);
      qPts.push(`${x.toFixed(1)},${yq.toFixed(1)}`);
    }

    return {
      start,
      windowLen,
      iPolyline: iPts.join(' '),
      qPolyline: qPts.join(' '),
    };
  }, [packed, iqWindowStart, iqWindowSpan, plotW, plotH]);

  const handleSvgMouseMove = (e: React.MouseEvent<SVGSVGElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const scaleX = width / rect.width;
    const svgX = (e.clientX - rect.left) * scaleX;
    if (svgX < padL || svgX > width - padR) {
      setHoverInfo(null);
      return;
    }
    const frac = (svgX - padL) / plotW;

    if (activePlot === 'fft' && fftData) {
      const freq = fftData.fStart + frac * (fftData.fEnd - fftData.fStart);
      const rawIdx = Math.round(((freq + sampleRateHz / 2) / sampleRateHz) * fftData.freqs.length);
      const idx = Math.max(0, Math.min(fftData.freqs.length - 1, rawIdx));
      const pDb = fftData.powers[idx];
      const dist = beatHzToDistanceM(freq, rangeConfig);
      setHoverInfo({
        x: svgX,
        label: `频率 f = ${freq >= 0 ? '+' : ''}${(freq / 1000).toFixed(2)} kHz · 对应距离 R = ${dist.toFixed(3)} m · 功率 ${pDb.toFixed(1)} dB`,
      });
    } else if (activePlot === 'czt' && cztData) {
      const idx = Math.max(
        0,
        Math.min(cztData.freqs.length - 1, Math.round(frac * (cztData.freqs.length - 1)))
      );
      const freq = cztData.freqs[idx];
      const pDb = cztData.powers[idx];
      const dist = beatHzToDistanceM(freq, rangeConfig);
      setHoverInfo({
        x: svgX,
        label: `CZT[${idx}] · f = ${freq.toFixed(2)} Hz · R = ${dist.toFixed(4)} m · ${pDb.toFixed(2)} dB`,
      });
    } else if (activePlot === 'iq') {
      const k = Math.max(0, Math.min(iqData.windowLen - 1, Math.round(frac * (iqData.windowLen - 1))));
      const sampleIdx = iqData.start + k;
      const word = packed[sampleIdx] ?? 0;
      const iVal = word & 0x0fff;
      const qVal = (word >>> 16) & 0x0fff;
      const iVolt = (iVal / 4095) * 3.3;
      const qVolt = (qVal / 4095) * 3.3;
      const timeUs = (sampleIdx / sampleRateHz) * 1e6;
      setHoverInfo({
        x: svgX,
        label: `采样点 n=${sampleIdx} (${timeUs.toFixed(1)} µs) · ADC1 I=${iVal} (${iVolt.toFixed(2)}V) · ADC2 Q=${qVal} (${qVolt.toFixed(2)}V)`,
      });
    }
  };

  return (
    <div className="bg-white border border-slate-200 rounded-lg overflow-hidden">
      {/* Toolbar */}
      <div className="flex flex-wrap items-center justify-between gap-3 px-4 py-2.5 border-b border-slate-200 bg-slate-50/70">
        <div className="flex items-center gap-1 p-0.5 bg-slate-200/70 rounded-md">
          <button
            type="button"
            onClick={() => {
              setActivePlot('iq');
              setHoverInfo(null);
            }}
            className={`px-3 py-1 text-xs font-medium rounded whitespace-nowrap shrink-0 transition-colors ${
              activePlot === 'iq'
                ? 'bg-white text-slate-900 shadow-xs'
                : 'text-slate-600 hover:text-slate-900'
            }`}
          >
            实时双通道 AD 波形 (I/Q)
          </button>
          <button
            type="button"
            onClick={() => {
              setActivePlot('fft');
              setHoverInfo(null);
            }}
            className={`px-3 py-1 text-xs font-medium rounded whitespace-nowrap shrink-0 transition-colors ${
              activePlot === 'fft'
                ? 'bg-white text-slate-900 shadow-xs'
                : 'text-slate-600 hover:text-slate-900'
            }`}
          >
            4096 点复数 FFT 频谱
          </button>
          <button
            type="button"
            onClick={() => {
              setActivePlot('czt');
              setHoverInfo(null);
            }}
            className={`px-3 py-1 text-xs font-medium rounded whitespace-nowrap shrink-0 transition-colors ${
              activePlot === 'czt'
                ? 'bg-white text-slate-900 shadow-xs'
                : 'text-slate-600 hover:text-slate-900'
            }`}
          >
            129 点 CZT 局部细化
          </button>
        </div>

        <div className="flex items-center gap-3 text-xs text-slate-500">
          {activePlot === 'iq' && (
            <div className="flex items-center gap-3">
              <div className="flex items-center gap-1 bg-slate-100 p-0.5 rounded border border-slate-200">
                {[120, 240, 480].map((span) => (
                  <button
                    key={span}
                    type="button"
                    onClick={() => setIqWindowSpan(span)}
                    className={`px-2 py-0.5 rounded font-mono text-[11px] whitespace-nowrap ${
                      iqWindowSpan === span ? 'bg-white text-slate-900' : 'text-slate-600'
                    }`}
                  >
                    {span}点
                  </button>
                ))}
              </div>
              <div className="flex items-center gap-2">
                <span className="font-mono text-slate-600 tabular-nums whitespace-nowrap">
                  [{iqData.start}..{iqData.start + iqData.windowLen - 1}]
                </span>
                <input
                  type="range"
                  min={0}
                  max={Math.max(0, packed.length - iqWindowSpan)}
                  step={20}
                  value={Math.min(iqWindowStart, Math.max(0, packed.length - iqWindowSpan))}
                  onChange={(e) => setIqWindowStart(Number(e.target.value))}
                  className="w-24 accent-blue-600"
                  aria-label="AD波形时间窗口偏移"
                />
              </div>
            </div>
          )}

          {activePlot === 'fft' && (
            <div className="flex items-center gap-1 bg-slate-100 p-0.5 rounded border border-slate-200">
              <button
                type="button"
                onClick={() => setFftSpanMode('activeSide')}
                className={`px-2 py-0.5 rounded text-xs font-medium whitespace-nowrap ${
                  fftSpanMode === 'activeSide' ? 'bg-white text-slate-900' : 'text-slate-600'
                }`}
              >
                主频带
              </button>
              <button
                type="button"
                onClick={() => setFftSpanMode('full')}
                className={`px-2 py-0.5 rounded text-xs font-medium whitespace-nowrap ${
                  fftSpanMode === 'full' ? 'bg-white text-slate-900' : 'text-slate-600'
                }`}
              >
                全频谱 ±fs/2
              </button>
            </div>
          )}

          {activePlot === 'czt' && result.czt && (
            <span className="font-mono tabular-nums text-slate-600 whitespace-nowrap">
              步进 Δf = {result.czt.result.gridSpacingHz.toFixed(3)} Hz · 跨度 ±1.5 FFT bin
            </span>
          )}
        </div>
      </div>

      {/* Crosshair Readout Strip */}
      <div className="flex items-center justify-between px-4 py-1.5 bg-white border-b border-slate-100 text-xs font-mono tabular-nums text-slate-600 min-h-[28px]">
        <span>
          {hoverInfo
            ? hoverInfo.label
            : activePlot === 'iq'
              ? 'ADC1 PA3 (IFI_H 实线) · ADC2 PA7 (IFQ_H 虚线) · TIM2 TRGO 400 kS/s 同步双采样 (12-bit)'
              : activePlot === 'fft'
                ? `汉宁窗 · 有效搜索 [${R120_SIG_MIN_BEAT_HZ} Hz .. ${(R120_SIG_MAX_BEAT_HZ / 1000).toFixed(0)} kHz] · 悬停读取频点与对应距离`
                : '局部直接 CZT 细化与三点二次插值 · 悬停读取精细频点'}
        </span>
        {activePlot === 'iq' && (
          <span className="text-slate-400 whitespace-nowrap">
            削波门限 ≤1 / ≥4094 ({result.clippedSamples} 点)
          </span>
        )}
      </div>

      {/* SVG Canvas */}
      <div className="p-2 bg-white">
        <svg
          viewBox={`0 0 ${width} ${height}`}
          className="w-full h-[270px] select-none cursor-crosshair block"
          onMouseMove={handleSvgMouseMove}
          onMouseLeave={() => setHoverInfo(null)}
        >
          <rect
            x={padL}
            y={padT}
            width={plotW}
            height={plotH}
            fill="#F8FAFC"
            stroke="#E2E8F0"
            strokeWidth={1}
          />

          {/* DUAL-ADC I/Q WAVEFORM */}
          {activePlot === 'iq' && (
            <>
              {[0, 1024, 2048, 3072, 4095].map((cnt) => {
                const y = padT + ((4095 - cnt) / 4095) * plotH;
                return (
                  <g key={cnt}>
                    <line
                      x1={padL}
                      y1={y}
                      x2={padL + plotW}
                      y2={y}
                      stroke={cnt === 2048 ? '#CBD5E1' : '#E2E8F0'}
                      strokeDasharray={cnt === 2048 ? undefined : '2 3'}
                    />
                    <text
                      x={padL - 8}
                      y={y + 4}
                      textAnchor="end"
                      className="fill-slate-400 font-mono text-[10px]"
                    >
                      {cnt}
                    </text>
                  </g>
                );
              })}

              {[0, 0.25, 0.5, 0.75, 1].map((t) => {
                const n = Math.round(iqData.start + t * (iqData.windowLen - 1));
                const timeUs = (n / sampleRateHz) * 1e6;
                const x = padL + t * plotW;
                return (
                  <g key={t}>
                    <line
                      x1={x}
                      y1={padT}
                      x2={x}
                      y2={padT + plotH}
                      stroke="#E2E8F0"
                      strokeDasharray="2 3"
                    />
                    <text
                      x={x}
                      y={height - 12}
                      textAnchor="middle"
                      className="fill-slate-500 font-mono text-[10px]"
                    >
                      {timeUs.toFixed(0)} µs
                    </text>
                  </g>
                );
              })}

              <polyline fill="none" stroke="#2563EB" strokeWidth={1.6} points={iqData.iPolyline} />
              <polyline
                fill="none"
                stroke="#0891B2"
                strokeWidth={1.5}
                strokeDasharray="4 2"
                points={iqData.qPolyline}
              />
            </>
          )}

          {/* FFT VIEW */}
          {activePlot === 'fft' && fftData && (
            <>
              {[-50, -25, 0, 25, 50].map((db) => {
                const y = padT + ((fftData.dbMax - db) / (fftData.dbMax - fftData.dbMin)) * plotH;
                return (
                  <g key={db}>
                    <line
                      x1={padL}
                      y1={y}
                      x2={padL + plotW}
                      y2={y}
                      stroke="#E2E8F0"
                      strokeDasharray="2 3"
                    />
                    <text
                      x={padL - 8}
                      y={y + 4}
                      textAnchor="end"
                      className="fill-slate-400 font-mono text-[10px]"
                    >
                      {db} dB
                    </text>
                  </g>
                );
              })}

              {[0, 0.25, 0.5, 0.75, 1].map((t) => {
                const f = fftData.fStart + t * (fftData.fEnd - fftData.fStart);
                const x = padL + t * plotW;
                return (
                  <g key={t}>
                    <line
                      x1={x}
                      y1={padT}
                      x2={x}
                      y2={padT + plotH}
                      stroke="#E2E8F0"
                      strokeDasharray="2 3"
                    />
                    <text
                      x={x}
                      y={height - 12}
                      textAnchor="middle"
                      className="fill-slate-500 font-mono text-[10px]"
                    >
                      {(f / 1000).toFixed(1)} kHz
                    </text>
                  </g>
                );
              })}

              {Number.isFinite(fftData.bgDb) &&
                (() => {
                  const clampedBg = Math.max(fftData.dbMin, Math.min(fftData.dbMax, fftData.bgDb));
                  const yBg =
                    padT + ((fftData.dbMax - clampedBg) / (fftData.dbMax - fftData.dbMin)) * plotH;
                  return (
                    <g>
                      <line
                        x1={padL}
                        y1={yBg}
                        x2={padL + plotW}
                        y2={yBg}
                        stroke="#F59E0B"
                        strokeWidth={1}
                        strokeDasharray="5 4"
                      />
                      <text
                        x={padL + plotW - 4}
                        y={Math.max(padT + 12, yBg - 5)}
                        textAnchor="end"
                        className="fill-amber-600 font-mono text-[10px]"
                      >
                        背景均值 {fftData.bgDb.toFixed(1)} dB
                      </text>
                    </g>
                  );
                })()}

              <polyline
                fill="none"
                stroke="#2563EB"
                strokeWidth={1.5}
                strokeLinejoin="round"
                points={fftData.polyline}
              />

              {result.fft &&
                result.fft.result.absBeatHz > 0 &&
                (() => {
                  const fPeak = result.fft.result.signedBeatHz;
                  if (fPeak < fftData.fStart || fPeak > fftData.fEnd) return null;
                  const xPeak =
                    padL +
                    ((fPeak - fftData.fStart) / Math.max(1, fftData.fEnd - fftData.fStart)) * plotW;
                  return (
                    <g>
                      <line
                        x1={xPeak}
                        y1={padT}
                        x2={xPeak}
                        y2={padT + plotH}
                        stroke="#0891B2"
                        strokeWidth={1.2}
                        strokeDasharray="3 3"
                      />
                      <text
                        x={Math.min(padL + plotW - 100, Math.max(padL + 6, xPeak + 6))}
                        y={padT + 14}
                        className="fill-cyan-700 font-mono text-[10px] font-semibold"
                      >
                        峰值 {(fPeak / 1000).toFixed(3)} kHz
                      </text>
                    </g>
                  );
                })()}
            </>
          )}

          {/* CZT VIEW */}
          {activePlot === 'czt' &&
            (cztData ? (
              <>
                {[0, 0.25, 0.5, 0.75, 1].map((t) => {
                  const db = cztData.dbTop - t * (cztData.dbTop - cztData.dbBot);
                  const y = padT + t * plotH;
                  return (
                    <g key={t}>
                      <line
                        x1={padL}
                        y1={y}
                        x2={padL + plotW}
                        y2={y}
                        stroke="#E2E8F0"
                        strokeDasharray="2 3"
                      />
                      <text
                        x={padL - 8}
                        y={y + 4}
                        textAnchor="end"
                        className="fill-slate-400 font-mono text-[10px]"
                      >
                        {db.toFixed(0)} dB
                      </text>
                    </g>
                  );
                })}

                {[0, 0.25, 0.5, 0.75, 1].map((t) => {
                  const f = cztData.fStart + t * (cztData.fEnd - cztData.fStart);
                  const x = padL + t * plotW;
                  return (
                    <g key={t}>
                      <line
                        x1={x}
                        y1={padT}
                        x2={x}
                        y2={padT + plotH}
                        stroke="#E2E8F0"
                        strokeDasharray="2 3"
                      />
                      <text
                        x={x}
                        y={height - 12}
                        textAnchor="middle"
                        className="fill-slate-500 font-mono text-[10px]"
                      >
                        {f.toFixed(1)} Hz
                      </text>
                    </g>
                  );
                })}

                {result.czt &&
                  (() => {
                    const xCoarse =
                      padL +
                      ((result.czt.result.coarseSignedBeatHz - cztData.fStart) /
                        Math.max(1e-6, cztData.fEnd - cztData.fStart)) *
                        plotW;
                    const xRefined =
                      padL +
                      ((result.czt.result.refinedSignedBeatHz - cztData.fStart) /
                        Math.max(1e-6, cztData.fEnd - cztData.fStart)) *
                        plotW;
                    return (
                      <>
                        <line
                          x1={xCoarse}
                          y1={padT}
                          x2={xCoarse}
                          y2={padT + plotH}
                          stroke="#94A3B8"
                          strokeWidth={1.2}
                          strokeDasharray="4 3"
                        />
                        <text
                          x={xCoarse - 6}
                          y={padT + 14}
                          textAnchor="end"
                          className="fill-slate-500 font-mono text-[10px]"
                        >
                          粗估 FFT
                        </text>
                        {result.czt.result.refinedSignedBeatHz !== 0 && (
                          <>
                            <line
                              x1={xRefined}
                              y1={padT}
                              x2={xRefined}
                              y2={padT + plotH}
                              stroke="#2563EB"
                              strokeWidth={1.5}
                            />
                            <text
                              x={xRefined + 6}
                              y={padT + 26}
                              className="fill-blue-700 font-mono text-[10px] font-semibold"
                            >
                              CZT {result.czt.result.refinedSignedBeatHz.toFixed(2)} Hz
                            </text>
                          </>
                        )}
                      </>
                    );
                  })()}

                <polyline
                  fill="none"
                  stroke="#0891B2"
                  strokeWidth={2}
                  strokeLinejoin="round"
                  points={cztData.polyline}
                />
              </>
            ) : (
              <text
                x={padL + plotW / 2}
                y={padT + plotH / 2}
                textAnchor="middle"
                className="fill-slate-400 text-xs"
              >
                CZT 已跳过 — 需要无削波且有效的 FFT 主峰。
              </text>
            ))}

          {hoverInfo && (
            <line
              x1={hoverInfo.x}
              y1={padT}
              x2={hoverInfo.x}
              y2={padT + plotH}
              stroke="#0F172A"
              strokeWidth={1}
              strokeDasharray="3 2"
            />
          )}
        </svg>
      </div>
    </div>
  );
};
