import React from 'react';
import { R120MeasFullResult, R120MeasStatus } from '../r120/r120Measure';

interface OledPreviewProps {
  result: R120MeasFullResult;
  tankHeightM: number;
  temperatureC: number;
  frameCount: number;
  isStreaming: boolean;
}

const OLED_STATUS_TEXT: Record<R120MeasStatus, string> = {
  [R120MeasStatus.Ok]: 'OK',
  [R120MeasStatus.BadArgument]: 'BAD_ARG',
  [R120MeasStatus.BadConfig]: 'BAD_CFG',
  [R120MeasStatus.BadSamples]: 'BAD_SMP',
  [R120MeasStatus.Clipped]: 'CLIPPED',
  [R120MeasStatus.NoTarget]: 'NO_TGT',
  [R120MeasStatus.CztFailed]: 'CZT_ERR',
  [R120MeasStatus.RangeFailed]: 'OUT_RNG',
  [R120MeasStatus.LowQuality]: 'LOW_COH',
  [R120MeasStatus.SelfTestFailed]: 'ST_FAIL',
};

export const OledPreview: React.FC<OledPreviewProps> = ({
  result,
  tankHeightM,
  temperatureC,
  frameCount,
  isStreaming,
}) => {
  const slantM = result.distanceValid && result.range ? result.range.distanceM : null;
  const liquidLevelMm =
    slantM !== null ? Math.max(0, Math.round((tankHeightM - slantM) * 1000)) : null;

  return (
    <div className="bg-white border border-slate-200 rounded-lg p-4 flex flex-col justify-between gap-3">
      <div className="flex items-center justify-between">
        <h3 className="text-xs font-semibold text-slate-700">SSD1306 OLED 实时显数</h3>
        <span className="text-xs font-mono text-slate-400 tabular-nums">
          {isStreaming ? '● 刷新中' : '○ 已暂停'} · #{frameCount}
        </span>
      </div>

      {/* Physical 128x64 monochrome OLED bezel */}
      <div className="bg-[#090D16] border border-slate-800 rounded p-3.5 font-mono text-[#38BDF8] tracking-wide select-none">
        <div className="flex items-center justify-between text-[11px] text-sky-400/80 border-b border-sky-900/60 pb-1.5 mb-2.5">
          <span>R120 FMCW</span>
          <span className="tabular-nums">{temperatureC.toFixed(2)}°C</span>
        </div>

        <div className="flex items-baseline justify-between py-1">
          <span className="text-sm text-sky-200">高度:</span>
          <span className="text-2xl font-semibold tabular-nums text-sky-300">
            {liquidLevelMm !== null ? `${liquidLevelMm}` : '----'}
            <span className="text-xs font-normal text-sky-400/80 ml-1">mm</span>
          </span>
        </div>

        <div className="flex items-center justify-between text-[11px] text-sky-400/75 mt-2 pt-1.5 border-t border-sky-900/50">
          <span className="tabular-nums">
            R={slantM !== null ? `${slantM.toFixed(3)}m` : '--.-m'}
          </span>
          <span
            className={
              result.status === R120MeasStatus.Ok
                ? 'text-sky-300'
                : result.status === R120MeasStatus.LowQuality
                  ? 'text-amber-300'
                  : 'text-rose-400'
            }
          >
            {OLED_STATUS_TEXT[result.status]}
          </span>
        </div>
      </div>

      <p className="text-xs text-slate-500 leading-relaxed">
        对应 <code className="font-mono text-slate-700">OLED_Update()</code>，液位 = 安装高度 (
        {tankHeightM.toFixed(2)} m) − 实时斜距。
      </p>
    </div>
  );
};
