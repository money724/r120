import React, { useMemo, useState } from 'react';
import { R120MeasStatus } from '../r120/r120Measure';

export interface DistanceHistoryPoint {
  frame: number;
  timestampMs: number;
  measuredDistanceM: number | null;
  trueDistanceM: number;
  liquidHeightMm: number | null;
  beatHz: number | null;
  contrastDb: number;
  coherence: number;
  status: R120MeasStatus;
}

interface DistanceTrendChartProps {
  history: DistanceHistoryPoint[];
  tankHeightM: number;
  onClearHistory: () => void;
  onExportCsv: () => void;
}

export const DistanceTrendChart: React.FC<DistanceTrendChartProps> = ({
  history,
  tankHeightM,
  onClearHistory,
  onExportCsv,
}) => {
  const [hoverIdx, setHoverIdx] = useState<number | null>(null);

  const width = 880;
  const height = 210;
  const padL = 58;
  const padR = 20;
  const padT = 18;
  const padB = 30;
  const plotW = width - padL - padR;
  const plotH = height - padT - padB;

  const stats = useMemo(() => {
    const valid = history.filter((h) => h.measuredDistanceM !== null);
    if (valid.length === 0) {
      return {
        minM: 0,
        maxM: 10,
        yMin: 0,
        yMax: 10,
        meanM: null as number | null,
        stdMm: null as number | null,
        rmsErrMm: null as number | null,
      };
    }

    let minVal = Infinity;
    let maxVal = -Infinity;
    let sum = 0;
    let errSqSum = 0;

    for (const pt of valid) {
      const m = pt.measuredDistanceM as number;
      if (m < minVal) minVal = m;
      if (m > maxVal) maxVal = m;
      if (pt.trueDistanceM < minVal) minVal = pt.trueDistanceM;
      if (pt.trueDistanceM > maxVal) maxVal = pt.trueDistanceM;
      sum += m;
      const err = m - pt.trueDistanceM;
      errSqSum += err * err;
    }

    const meanM = sum / valid.length;
    let varSum = 0;
    for (const pt of valid) {
      const d = (pt.measuredDistanceM as number) - meanM;
      varSum += d * d;
    }
    const stdMm = Math.sqrt(varSum / valid.length) * 1000;
    const rmsErrMm = Math.sqrt(errSqSum / valid.length) * 1000;

    const span = Math.max(0.12, maxVal - minVal);
    const margin = span * 0.22;
    const yMin = Math.max(0, minVal - margin);
    const yMax = maxVal + margin;

    return { minM: minVal, maxM: maxVal, yMin, yMax, meanM, stdMm, rmsErrMm };
  }, [history]);

  const { measuredPath, truePath } = useMemo(() => {
    if (history.length === 0) return { measuredPath: '', truePath: '' };
    const count = history.length;
    const xOf = (i: number) =>
      count === 1 ? padL + plotW : padL + (i / (count - 1)) * plotW;
    const yOf = (val: number) =>
      padT + ((stats.yMax - val) / Math.max(1e-6, stats.yMax - stats.yMin)) * plotH;

    const truePts: string[] = [];
    const measCmds: string[] = [];
    let penDown = false;

    for (let i = 0; i < count; ++i) {
      const pt = history[i];
      const x = xOf(i);
      truePts.push(`${x.toFixed(1)},${yOf(pt.trueDistanceM).toFixed(1)}`);

      if (pt.measuredDistanceM !== null) {
        const y = yOf(pt.measuredDistanceM);
        measCmds.push(`${penDown ? 'L' : 'M'}${x.toFixed(1)},${y.toFixed(1)}`);
        penDown = true;
      } else {
        penDown = false;
      }
    }

    return {
      measuredPath: measCmds.join(' '),
      truePath: truePts.join(' '),
    };
  }, [history, stats.yMax, stats.yMin, plotW, plotH]);

  const handleMouseMove = (e: React.MouseEvent<SVGSVGElement>) => {
    if (history.length === 0) {
      setHoverIdx(null);
      return;
    }
    const rect = e.currentTarget.getBoundingClientRect();
    const scaleX = width / rect.width;
    const svgX = (e.clientX - rect.left) * scaleX;
    if (svgX < padL || svgX > width - padR) {
      setHoverIdx(null);
      return;
    }
    const frac = (svgX - padL) / plotW;
    const idx = Math.max(0, Math.min(history.length - 1, Math.round(frac * (history.length - 1))));
    setHoverIdx(idx);
  };

  const hovered = hoverIdx !== null ? history[hoverIdx] : null;
  const hoverX =
    hoverIdx !== null && history.length > 1
      ? padL + (hoverIdx / (history.length - 1)) * plotW
      : padL + plotW;

  return (
    <div className="bg-white border border-slate-200 rounded-lg overflow-hidden">
      <div className="flex flex-wrap items-center justify-between gap-3 px-4 py-2.5 border-b border-slate-200 bg-slate-50/70">
        <div className="flex items-center gap-3">
          <h2 className="text-xs font-semibold text-slate-800">实时距离计算趋势 (最近 120 帧)</h2>
          <span className="text-xs font-mono tabular-nums text-slate-500">
            均值 {stats.meanM !== null ? `${stats.meanM.toFixed(4)} m` : '---'} · RMS 误差{' '}
            {stats.rmsErrMm !== null ? `${stats.rmsErrMm.toFixed(2)} mm` : '---'} · 波动 σ{' '}
            {stats.stdMm !== null ? `${stats.stdMm.toFixed(2)} mm` : '---'}
          </span>
        </div>

        <div className="flex items-center gap-2">
          <button
            type="button"
            onClick={onExportCsv}
            disabled={history.length === 0}
            className="px-2.5 py-1 text-xs font-medium text-slate-700 bg-white border border-slate-200 rounded hover:bg-slate-50 disabled:opacity-40 whitespace-nowrap"
          >
            导出 CSV
          </button>
          <button
            type="button"
            onClick={onClearHistory}
            disabled={history.length === 0}
            className="px-2.5 py-1 text-xs font-medium text-slate-600 bg-white border border-slate-200 rounded hover:bg-slate-50 disabled:opacity-40 whitespace-nowrap"
          >
            清空曲线
          </button>
        </div>
      </div>

      <div className="flex items-center justify-between px-4 py-1.5 border-b border-slate-100 text-xs font-mono tabular-nums text-slate-600 min-h-[28px]">
        <span>
          {hovered
            ? `帧 #${hovered.frame} · 解算斜距 ${
                hovered.measuredDistanceM !== null ? `${hovered.measuredDistanceM.toFixed(4)} m` : '无效'
              } · 液位 ${
                hovered.liquidHeightMm !== null ? `${hovered.liquidHeightMm} mm` : '---'
              } · 参考 ${hovered.trueDistanceM.toFixed(4)} m · 对比度 ${hovered.contrastDb.toFixed(1)} dB`
            : `实线：实时解算斜距 (m) · 虚线：目标真实斜距 (m) · 罐高基准 ${tankHeightM.toFixed(2)} m · 悬停读取单帧`}
        </span>
        <span className="text-slate-400 whitespace-nowrap">已采集 {history.length} 帧</span>
      </div>

      <div className="p-2 bg-white">
        <svg
          viewBox={`0 0 ${width} ${height}`}
          className="w-full h-[210px] select-none cursor-crosshair block"
          onMouseMove={handleMouseMove}
          onMouseLeave={() => setHoverIdx(null)}
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

          {[0, 0.25, 0.5, 0.75, 1].map((t) => {
            const val = stats.yMax - t * (stats.yMax - stats.yMin);
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
                  {val.toFixed(3)}m
                </text>
              </g>
            );
          })}

          {history.length > 0 &&
            [0, 0.25, 0.5, 0.75, 1].map((t) => {
              const idx = Math.round(t * (history.length - 1));
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
                    y={height - 10}
                    textAnchor="middle"
                    className="fill-slate-500 font-mono text-[10px]"
                  >
                    #{history[idx].frame}
                  </text>
                </g>
              );
            })}

          {/* Reference target distance */}
          {truePath && (
            <polyline
              fill="none"
              stroke="#94A3B8"
              strokeWidth={1.2}
              strokeDasharray="4 3"
              points={truePath}
            />
          )}

          {/* Measured distance */}
          {measuredPath && (
            <path
              fill="none"
              stroke="#2563EB"
              strokeWidth={2}
              strokeLinejoin="round"
              d={measuredPath}
            />
          )}

          {/* Invalid frame markers */}
          {history.map((pt, i) => {
            if (pt.measuredDistanceM !== null) return null;
            const x =
              history.length === 1
                ? padL + plotW
                : padL + (i / (history.length - 1)) * plotW;
            return (
              <line
                key={pt.frame}
                x1={x}
                y1={padT}
                x2={x}
                y2={padT + plotH}
                stroke="#F43F5E"
                strokeWidth={1}
                strokeOpacity={0.45}
              />
            );
          })}

          {hovered && (
            <line
              x1={hoverX}
              y1={padT}
              x2={hoverX}
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
