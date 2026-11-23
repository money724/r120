import React, { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  R120MeasFullResult,
  R120MeasStatus,
  SyntheticFrameParams,
  analyzePackedFrame,
  makeMeasureConfig,
  synthesizePackedFrame,
} from './r120/r120Measure';
import { R120SigStatus } from './r120/r120Signal';
import { R120CztStatus } from './r120/r120Czt';
import { R120RangeStatus, makeRangeConfig } from './r120/r120Range';
import { R120PhaseStatus } from './r120/r120Phase';
import { formatHex32 } from './r120/adf4159Config';
import { SpectrumCharts } from './components/SpectrumCharts';
import {
  DistanceHistoryPoint,
  DistanceTrendChart,
} from './components/DistanceTrendChart';
import { OledPreview } from './components/OledPreview';
import { Adf4159PlannerView } from './components/Adf4159PlannerView';
import { PeripheralsView } from './components/PeripheralsView';
import { SelfTestView } from './components/SelfTestView';
import { Pause, Play, RotateCcw, StepForward } from 'lucide-react';

type ActiveView = 'pipeline' | 'adf4159' | 'peripherals' | 'selftest';
type MotionProfile = 'ripple' | 'filling' | 'draining' | 'step' | 'manual';

const STATUS_LABEL_ZH: Record<R120MeasStatus, string> = {
  [R120MeasStatus.Ok]: '正常锁定',
  [R120MeasStatus.BadArgument]: '参数无效',
  [R120MeasStatus.BadConfig]: '配置异常',
  [R120MeasStatus.BadSamples]: '样本异常',
  [R120MeasStatus.Clipped]: 'ADC 削波',
  [R120MeasStatus.NoTarget]: '未检出主峰',
  [R120MeasStatus.CztFailed]: 'CZT 失败',
  [R120MeasStatus.RangeFailed]: '超出量程',
  [R120MeasStatus.LowQuality]: '相干度偏低',
  [R120MeasStatus.SelfTestFailed]: '自检失败',
};

const MOTION_OPTIONS: Array<{ id: MotionProfile; label: string }> = [
  { id: 'ripple', label: '液面微幅波动' },
  { id: 'filling', label: '注液上升' },
  { id: 'draining', label: '排液下降' },
  { id: 'step', label: '阶跃跳变' },
  { id: 'manual', label: '手动定点' },
];

const DEFAULT_SWEEP = {
  bandwidthHz: 5_000_000_000,
  durationUs: 6000,
  startGhz: 120.0,
  stopGhz: 125.0,
  minDistanceM: 0.0,
  maxDistanceM: 15.0,
  distanceOffsetM: 0.0,
  sampleRateHz: 400_000,
  sampleCount: 2400,
};

const DEFAULT_SIM: SyntheticFrameParams = {
  targetDistanceM: 5.0,
  targetAmplitudeCounts: 680,
  initialPhaseRad: 0.7,
  noiseStdCounts: 22,
  iqPolarity: 1,
  dcBiasI: 2048,
  dcBiasQ: 2030,
  enableClutter: false,
  clutterDistanceM: 2.3,
  clutterAmplitudeCounts: 260,
  seed: 1024,
};

const MAX_HISTORY_POINTS = 120;

interface LiveFrameSnapshot {
  frameCount: number;
  packed: Uint32Array;
  expectedTargetBeatHz: number;
  trueDistanceM: number;
  result: R120MeasFullResult;
  computeMs: number;
  firstI: number;
  firstQ: number;
  lastRaw: number;
  acqStateLabel: string;
}

function computeDynamicDistance(
  profile: MotionProfile,
  baseDistanceM: number,
  elapsedSec: number
): number {
  switch (profile) {
    case 'ripple':
      // Gentle industrial tank surface wave (~±35 mm + slow secondary drift)
      return Math.max(
        0.3,
        Math.min(
          14.5,
          baseDistanceM +
            0.032 * Math.sin(elapsedSec * 1.4) +
            0.014 * Math.cos(elapsedSec * 3.1)
        )
      );
    case 'filling': {
      // Slant distance decreases as liquid rises (sawtooth loop 7.2 m -> 1.6 m)
      const cycle = (elapsedSec * 0.28) % 5.6;
      return 7.2 - cycle;
    }
    case 'draining': {
      // Slant distance increases as liquid drains (sawtooth loop 1.6 m -> 7.2 m)
      const cycle = (elapsedSec * 0.28) % 5.6;
      return 1.6 + cycle;
    }
    case 'step': {
      const phase = Math.floor(elapsedSec / 3.0) % 2;
      return phase === 0 ? 2.8 : 6.4;
    }
    case 'manual':
    default:
      return baseDistanceM;
  }
}

export function App(): React.ReactElement {
  const [activeView, setActiveView] = useState<ActiveView>('pipeline');
  const [sweep, setSweep] = useState(DEFAULT_SWEEP);
  const [sim, setSim] = useState<SyntheticFrameParams>(DEFAULT_SIM);
  const [tankHeightM, setTankHeightM] = useState<number>(8.0);
  const [powerStage, setPowerStage] = useState<number>(5);
  const [temperatureC, setTemperatureC] = useState<number>(26.5);

  // Real-time continuous AD acquisition controls
  const [isStreaming, setIsStreaming] = useState<boolean>(true);
  const [frameRateHz, setFrameRateHz] = useState<number>(10);
  const [motionProfile, setMotionProfile] = useState<MotionProfile>('ripple');
  const [history, setHistory] = useState<DistanceHistoryPoint[]>([]);

  const rangeCfg = useMemo(() => {
    return makeRangeConfig(
      sweep.bandwidthHz,
      sweep.durationUs,
      sweep.minDistanceM,
      sweep.maxDistanceM,
      sweep.distanceOffsetM
    ).config;
  }, [sweep]);

  const measCfg = useMemo(() => {
    return makeMeasureConfig(rangeCfg, sweep.sampleRateHz, sweep.sampleCount).config;
  }, [rangeCfg, sweep.sampleRateHz, sweep.sampleCount]);

  // Refs for real-time timer loop without stale closures
  const frameCounterRef = useRef<number>(0);
  const startTimeRef = useRef<number>(performance.now());
  const simRef = useRef(sim);
  const measCfgRef = useRef(measCfg);
  const tankHeightRef = useRef(tankHeightM);
  const motionProfileRef = useRef(motionProfile);

  simRef.current = sim;
  measCfgRef.current = measCfg;
  tankHeightRef.current = tankHeightM;
  motionProfileRef.current = motionProfile;

  const acquireOneFrame = useCallback((): LiveFrameSnapshot => {
    const t0 = performance.now();
    const elapsedSec = (t0 - startTimeRef.current) / 1000;
    const nextFrame = frameCounterRef.current + 1;
    frameCounterRef.current = nextFrame;

    const currentSim = simRef.current;
    const currentCfg = measCfgRef.current;
    const profile = motionProfileRef.current;

    const dynamicDistM = computeDynamicDistance(
      profile,
      currentSim.targetDistanceM,
      elapsedSec
    );

    // Carrier phase evolves naturally with tiny path-length change (lambda_120GHz ~ 2.5 mm)
    const phaseShift =
      profile === 'manual'
        ? currentSim.initialPhaseRad
        : ((4 * Math.PI * (dynamicDistM - currentSim.targetDistanceM)) / 0.002498 +
            currentSim.initialPhaseRad) %
          (2 * Math.PI);

    const frameParams: SyntheticFrameParams = {
      ...currentSim,
      targetDistanceM: dynamicDistM,
      initialPhaseRad: phaseShift,
      seed: (currentSim.seed + nextFrame * 7919) >>> 0,
    };

    const synth = synthesizePackedFrame(currentCfg, frameParams);
    const result = analyzePackedFrame(synth.packed, currentCfg.sampleCount, currentCfg);
    const computeMs = performance.now() - t0;

    const firstWord = synth.packed[0] ?? 0;
    const lastWord = synth.packed[currentCfg.sampleCount - 1] ?? 0;

    const measuredM = result.distanceValid && result.range ? result.range.distanceM : null;
    const liquidMm =
      measuredM !== null
        ? Math.max(0, Math.round((tankHeightRef.current - measuredM) * 1000))
        : null;

    const point: DistanceHistoryPoint = {
      frame: nextFrame,
      timestampMs: Date.now(),
      measuredDistanceM: measuredM,
      trueDistanceM: dynamicDistM,
      liquidHeightMm: liquidMm,
      beatHz:
        result.czt && result.cztStatus === R120CztStatus.Ok
          ? result.czt.result.refinedSignedBeatHz
          : null,
      contrastDb: result.fft ? result.fft.result.peakContrastDb : 0,
      coherence: result.phase ? result.phase.coherentFraction : 0,
      status: result.status,
    };

    setHistory((prev) => {
      const next = prev.length >= MAX_HISTORY_POINTS ? prev.slice(prev.length - MAX_HISTORY_POINTS + 1) : prev;
      return [...next, point];
    });

    return {
      frameCount: nextFrame,
      packed: synth.packed,
      expectedTargetBeatHz: synth.expectedTargetBeatHz,
      trueDistanceM: dynamicDistM,
      result,
      computeMs,
      firstI: firstWord & 0x0fff,
      firstQ: (firstWord >>> 16) & 0x0fff,
      lastRaw: lastWord,
      acqStateLabel: 'R120_ACQ_STATUS_DONE',
    };
  }, []);

  const [snapshot, setSnapshot] = useState<LiveFrameSnapshot>(() => acquireOneFrame());

  // Continuous real-time AD acquisition loop
  useEffect(() => {
    if (!isStreaming) return;
    const intervalMs = Math.max(50, Math.round(1000 / frameRateHz));
    const id = window.setInterval(() => {
      setSnapshot(acquireOneFrame());
    }, intervalMs);
    return () => window.clearInterval(id);
  }, [isStreaming, frameRateHz, acquireOneFrame]);

  // Trigger an immediate refresh when user adjusts parameters while paused
  const handleManualSingleShot = () => {
    setSnapshot(acquireOneFrame());
  };

  const handleResetDefaults = () => {
    setSweep(DEFAULT_SWEEP);
    setSim(DEFAULT_SIM);
    setTankHeightM(8.0);
    setMotionProfile('ripple');
    setFrameRateHz(10);
    setHistory([]);
    frameCounterRef.current = 0;
    startTimeRef.current = performance.now();
    setSnapshot(acquireOneFrame());
  };

  const handleApplyAdfPlan = (
    bandwidthHz: number,
    durationUs: number,
    startGhz: number,
    stopGhz: number
  ) => {
    setSweep((prev) => ({
      ...prev,
      bandwidthHz,
      durationUs,
      startGhz,
      stopGhz,
    }));
  };

  const handleExportCsv = () => {
    if (history.length === 0) return;
    const header =
      'frame,timestamp_ms,true_distance_m,measured_distance_m,liquid_height_mm,czt_beat_hz,contrast_db,coherence,status\n';
    const body = history
      .map((h) =>
        [
          h.frame,
          h.timestampMs,
          h.trueDistanceM.toFixed(5),
          h.measuredDistanceM !== null ? h.measuredDistanceM.toFixed(5) : '',
          h.liquidHeightMm !== null ? h.liquidHeightMm : '',
          h.beatHz !== null ? h.beatHz.toFixed(3) : '',
          h.contrastDb.toFixed(2),
          h.coherence.toFixed(4),
          h.status,
        ].join(',')
      )
      .join('\n');
    const blob = new Blob([header + body], { type: 'text/csv;charset=utf-8;' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `r120_realtime_distance_${Date.now()}.csv`;
    a.click();
    URL.revokeObjectURL(url);
  };

  const { result, packed, expectedTargetBeatHz } = snapshot;

  const statusTone =
    result.status === R120MeasStatus.Ok
      ? 'text-emerald-700'
      : result.status === R120MeasStatus.LowQuality
        ? 'text-amber-700'
        : 'text-rose-700';

  const statusGlyph =
    result.status === R120MeasStatus.Ok
      ? '●'
      : result.status === R120MeasStatus.LowQuality
        ? '▲'
        : '✖';

  const measuredDistanceM = result.distanceValid && result.range ? result.range.distanceM : null;
  const liquidLevelM =
    measuredDistanceM !== null ? Math.max(0, tankHeightM - measuredDistanceM) : null;
  const cztDeltaHz =
    result.czt && result.fft
      ? result.czt.result.refinedSignedBeatHz - result.fft.result.signedBeatHz
      : null;

  const pipelineStages = [
    {
      step: '01',
      name: 'DMA 双通道采集与削波检查',
      ok: result.clippedFraction <= measCfg.maxClippedFraction,
      detail: `2400 对样本 · 削波 ${result.clippedSamples} 点 (${(
        result.clippedFraction * 100
      ).toFixed(2)}% ≤ 0.50%)`,
    },
    {
      step: '02',
      name: '4096 点复数 FFT 粗估',
      ok: result.fftStatus === R120SigStatus.Ok,
      detail: result.fft
        ? `Bin ${result.fft.result.peakBin} · ${result.fft.result.signedBeatHz.toFixed(1)} Hz · 对比度 ${result.fft.result.peakContrastDb.toFixed(1)} dB`
        : '未执行',
    },
    {
      step: '03',
      name: '129 点局部 CZT 频峰细化',
      ok: result.cztStatus === R120CztStatus.Ok,
      detail: result.czt
        ? `${result.czt.result.refinedSignedBeatHz.toFixed(3)} Hz · 步进 ${result.czt.result.gridSpacingHz.toFixed(2)} Hz`
        : '已跳过',
    },
    {
      step: '04',
      name: 'FMCW 斜距解算',
      ok: result.rangeStatus === R120RangeStatus.Ok,
      detail: result.range
        ? `${result.range.distanceM.toFixed(4)} m · 斜率 ${(result.range.slopeHzPerS / 1e12).toFixed(4)} THz/s`
        : '已跳过',
    },
    {
      step: '05',
      name: '相位与单音相干性门限',
      ok: result.phaseStatus === R120PhaseStatus.Ok && result.phaseValid,
      detail: result.phase
        ? `${result.phase.phaseRad.toFixed(4)} rad (${result.phase.phaseDeg.toFixed(1)}°) · ${(
            result.phase.coherentFraction * 100
          ).toFixed(1)}%`
        : '已跳过',
    },
  ];

  return (
    <div className="min-h-screen flex flex-col bg-[#F8FAFC] text-slate-900">
      {/* Strict 3-Zone Top Bar Contract */}
      <header className="sticky top-0 z-20 bg-white border-b border-slate-200 px-6 h-14 flex items-center justify-between gap-8">
        <a
          href="#pipeline"
          onClick={(e) => {
            e.preventDefault();
            setActiveView('pipeline');
          }}
          className="text-base font-semibold tracking-tight text-slate-900 whitespace-nowrap shrink-0"
        >
          R120 雷达实时测距台
        </a>

        <nav className="flex items-center gap-6 overflow-x-auto text-sm font-medium">
          {(
            [
              { id: 'pipeline', label: '实时采集测距' },
              { id: 'adf4159', label: 'ADF4159 扫频规划' },
              { id: 'peripherals', label: '外设与电源' },
              { id: 'selftest', label: '固件自检' },
            ] as const
          ).map((item) => {
            const active = activeView === item.id;
            return (
              <button
                key={item.id}
                type="button"
                onClick={() => setActiveView(item.id)}
                className={`h-14 border-b-2 transition-colors whitespace-nowrap shrink-0 ${
                  active
                    ? 'border-blue-600 text-slate-900'
                    : 'border-transparent text-slate-500 hover:text-slate-900'
                }`}
              >
                {item.label}
              </button>
            );
          })}
        </nav>

        <div className="shrink-0">
          <button
            type="button"
            onClick={() => setIsStreaming((s) => !s)}
            className={`inline-flex items-center gap-1.5 px-4 py-2 text-xs font-medium rounded-md transition-colors whitespace-nowrap shrink-0 ${
              isStreaming
                ? 'bg-slate-900 text-white hover:bg-slate-800'
                : 'bg-blue-600 text-white hover:bg-blue-700'
            }`}
          >
            {isStreaming ? <Pause className="w-3.5 h-3.5" /> : <Play className="w-3.5 h-3.5" />}
            {isStreaming ? '暂停实时采集' : '启动实时采集'}
          </button>
        </div>
      </header>

      {/* Main Content */}
      <main className="flex-1 w-full max-w-[1440px] mx-auto px-6 py-6">
        {activeView === 'pipeline' && (
          <div className="grid grid-cols-1 lg:grid-cols-[340px_1fr] gap-6 items-start">
            {/* Left Real-Time AD Acquisition & Target Control Column */}
            <aside className="bg-white border border-slate-200 rounded-lg p-5 space-y-5">
              <div className="flex items-center justify-between">
                <h1 className="text-sm font-semibold text-slate-900">实时 AD 采集控制</h1>
                <button
                  type="button"
                  onClick={handleResetDefaults}
                  className="inline-flex items-center gap-1 text-xs text-slate-500 hover:text-slate-900 transition-colors whitespace-nowrap"
                >
                  <RotateCcw className="w-3.5 h-3.5" />
                  重置
                </button>
              </div>

              {/* Acquisition Trigger & Frame Rate */}
              <div className="space-y-3 bg-slate-50 border border-slate-200 rounded-md p-3.5">
                <div className="flex items-center justify-between text-xs">
                  <span className="font-medium text-slate-700">采集状态</span>
                  <span
                    className={`font-mono font-semibold ${
                      isStreaming ? 'text-emerald-700' : 'text-slate-500'
                    }`}
                  >
                    {isStreaming ? `● 连续采集中 (${frameRateHz} Hz)` : '○ 已暂停'}
                  </span>
                </div>

                <div>
                  <div className="text-xs text-slate-600 mb-1.5">实时帧触发频率 (DMA 帧率)</div>
                  <div className="grid grid-cols-4 gap-1 p-0.5 bg-slate-200/70 rounded">
                    {[2, 5, 10, 20].map((hz) => (
                      <button
                        key={hz}
                        type="button"
                        onClick={() => setFrameRateHz(hz)}
                        className={`py-1 text-xs font-mono rounded transition-colors ${
                          frameRateHz === hz
                            ? 'bg-white text-slate-900 font-semibold shadow-xs'
                            : 'text-slate-600 hover:text-slate-900'
                        }`}
                      >
                        {hz}Hz
                      </button>
                    ))}
                  </div>
                </div>

                <div className="grid grid-cols-2 gap-2 pt-1">
                  <button
                    type="button"
                    onClick={() => setIsStreaming((s) => !s)}
                    className="inline-flex items-center justify-center gap-1.5 py-1.5 px-2.5 text-xs font-medium bg-white border border-slate-300 rounded text-slate-800 hover:bg-slate-50 whitespace-nowrap"
                  >
                    {isStreaming ? <Pause className="w-3.5 h-3.5" /> : <Play className="w-3.5 h-3.5" />}
                    {isStreaming ? '暂停' : '继续'}
                  </button>
                  <button
                    type="button"
                    onClick={handleManualSingleShot}
                    className="inline-flex items-center justify-center gap-1.5 py-1.5 px-2.5 text-xs font-medium bg-blue-600 text-white rounded hover:bg-blue-700 whitespace-nowrap"
                  >
                    <StepForward className="w-3.5 h-3.5" />
                    单帧触发
                  </button>
                </div>
              </div>

              {/* Dynamic Target Motion Profile */}
              <div className="space-y-2.5">
                <div className="text-xs font-medium text-slate-500">液面/目标运动工况</div>
                <div className="grid grid-cols-2 gap-1.5">
                  {MOTION_OPTIONS.map((opt) => (
                    <button
                      key={opt.id}
                      type="button"
                      onClick={() => setMotionProfile(opt.id)}
                      className={`py-1.5 px-2.5 text-xs rounded border text-left transition-colors whitespace-nowrap truncate ${
                        motionProfile === opt.id
                          ? 'border-blue-600 bg-blue-50/70 text-blue-800 font-medium'
                          : 'border-slate-200 bg-white text-slate-600 hover:bg-slate-50'
                      }`}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>

              {/* Target & IF Signal Parameters */}
              <div className="space-y-3.5 pt-4 border-t border-slate-100">
                <div className="text-xs font-medium text-slate-500">目标与中频回波参数</div>

                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-slate-700">
                      {motionProfile === 'manual' ? '目标斜距' : '基准斜距'}
                    </span>
                    <span className="font-mono tabular-nums font-medium text-slate-900">
                      {sim.targetDistanceM.toFixed(2)} m
                    </span>
                  </div>
                  <input
                    type="range"
                    min={0.3}
                    max={14.5}
                    step={0.05}
                    value={sim.targetDistanceM}
                    onChange={(e) => {
                      const val = Number(e.target.value);
                      setSim((prev) => ({ ...prev, targetDistanceM: val }));
                      if (!isStreaming) {
                        simRef.current = { ...simRef.current, targetDistanceM: val };
                        setSnapshot(acquireOneFrame());
                      }
                    }}
                    className="w-full accent-blue-600"
                  />
                </div>

                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-slate-700">雷达安装高度 (罐顶基准)</span>
                    <span className="font-mono tabular-nums font-medium text-slate-900">
                      {tankHeightM.toFixed(2)} m
                    </span>
                  </div>
                  <input
                    type="range"
                    min={1.0}
                    max={15.0}
                    step={0.1}
                    value={tankHeightM}
                    onChange={(e) => setTankHeightM(Number(e.target.value))}
                    className="w-full accent-blue-600"
                  />
                </div>

                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-slate-700">回波幅度 (ADC 码值)</span>
                    <span className="font-mono tabular-nums font-medium text-slate-900">
                      {sim.targetAmplitudeCounts} cnt
                    </span>
                  </div>
                  <input
                    type="range"
                    min={10}
                    max={2300}
                    step={10}
                    value={sim.targetAmplitudeCounts}
                    onChange={(e) =>
                      setSim((prev) => ({ ...prev, targetAmplitudeCounts: Number(e.target.value) }))
                    }
                    className="w-full accent-blue-600"
                  />
                  <div className="flex justify-between text-[11px] font-mono text-slate-400 mt-0.5">
                    <span>10 (弱回波)</span>
                    <span>680 (典型)</span>
                    <span>2300 (削波)</span>
                  </div>
                </div>

                <div>
                  <div className="flex justify-between text-xs mb-1">
                    <span className="text-slate-700">中频底噪 (σ)</span>
                    <span className="font-mono tabular-nums font-medium text-slate-900">
                      {sim.noiseStdCounts} cnt
                    </span>
                  </div>
                  <input
                    type="range"
                    min={0}
                    max={600}
                    step={5}
                    value={sim.noiseStdCounts}
                    onChange={(e) =>
                      setSim((prev) => ({ ...prev, noiseStdCounts: Number(e.target.value) }))
                    }
                    className="w-full accent-blue-600"
                  />
                </div>
              </div>

              {/* I/Q Polarity & Clutter */}
              <div className="pt-4 border-t border-slate-100 space-y-3">
                <div className="flex items-center justify-between text-xs">
                  <span className="text-slate-700 font-medium">I/Q 正交极性</span>
                  <div className="flex items-center gap-1 p-0.5 bg-slate-100 rounded border border-slate-200">
                    <button
                      type="button"
                      onClick={() => setSim((prev) => ({ ...prev, iqPolarity: 1 }))}
                      className={`px-2 py-0.5 rounded font-mono text-xs ${
                        sim.iqPolarity === 1 ? 'bg-white text-slate-900 shadow-xs' : 'text-slate-500'
                      }`}
                    >
                      +Beat
                    </button>
                    <button
                      type="button"
                      onClick={() => setSim((prev) => ({ ...prev, iqPolarity: -1 }))}
                      className={`px-2 py-0.5 rounded font-mono text-xs ${
                        sim.iqPolarity === -1 ? 'bg-white text-slate-900 shadow-xs' : 'text-slate-500'
                      }`}
                    >
                      -Beat
                    </button>
                  </div>
                </div>

                <label className="flex items-center justify-between text-xs cursor-pointer">
                  <span className="text-slate-700 font-medium">注入罐壁杂波干扰</span>
                  <input
                    type="checkbox"
                    checked={sim.enableClutter}
                    onChange={(e) =>
                      setSim((prev) => ({ ...prev, enableClutter: e.target.checked }))
                    }
                    className="h-4 w-4 rounded accent-blue-600"
                  />
                </label>

                {sim.enableClutter && (
                  <div className="space-y-2.5 bg-slate-50 border border-slate-200 rounded-md p-3">
                    <div>
                      <div className="flex justify-between text-xs mb-1">
                        <span className="text-slate-600">干扰目标距离</span>
                        <span className="font-mono tabular-nums text-slate-900">
                          {sim.clutterDistanceM.toFixed(2)} m
                        </span>
                      </div>
                      <input
                        type="range"
                        min={0.5}
                        max={12.0}
                        step={0.1}
                        value={sim.clutterDistanceM}
                        onChange={(e) =>
                          setSim((prev) => ({ ...prev, clutterDistanceM: Number(e.target.value) }))
                        }
                        className="w-full accent-blue-600"
                      />
                    </div>
                    <div>
                      <div className="flex justify-between text-xs mb-1">
                        <span className="text-slate-600">干扰幅度</span>
                        <span className="font-mono tabular-nums text-slate-900">
                          {sim.clutterAmplitudeCounts} cnt
                        </span>
                      </div>
                      <input
                        type="range"
                        min={50}
                        max={900}
                        step={10}
                        value={sim.clutterAmplitudeCounts}
                        onChange={(e) =>
                          setSim((prev) => ({
                            ...prev,
                            clutterAmplitudeCounts: Number(e.target.value),
                          }))
                        }
                        className="w-full accent-blue-600"
                      />
                    </div>
                  </div>
                )}
              </div>

              {/* Active Hardware Config Summary */}
              <div className="pt-4 border-t border-slate-100 space-y-2 text-xs">
                <div className="flex items-center justify-between">
                  <span className="font-medium text-slate-500">当前扫频与 ADC 配置</span>
                  <button
                    type="button"
                    onClick={() => setActiveView('adf4159')}
                    className="text-blue-600 hover:underline font-medium whitespace-nowrap"
                  >
                    修改扫频
                  </button>
                </div>
                <div className="grid grid-cols-2 gap-y-1 font-mono text-[11px] tabular-nums text-slate-700">
                  <span className="text-slate-400">RF 带宽 / 时长</span>
                  <span className="text-right">
                    {(sweep.bandwidthHz / 1e9).toFixed(3)} GHz / {sweep.durationUs} µs
                  </span>
                  <span className="text-slate-400">ADC1 / ADC2</span>
                  <span className="text-right">PA3 / PA7 · 400 kS/s</span>
                  <span className="text-slate-400">TIM2 TRGO</span>
                  <span className="text-right">Period=224 · 2400 pts</span>
                </div>
              </div>
            </aside>

            {/* Right Real-Time Telemetry & Oscilloscope Stage */}
            <section className="space-y-4 min-w-0">
              {/* 4-Column Real-Time Metric Strip */}
              <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-4 gap-3.5">
                <div className="bg-white border border-slate-200 rounded-lg p-4">
                  <div className="text-xs text-slate-500">实时解算斜距</div>
                  <div className="text-2xl font-mono font-semibold text-slate-900 tabular-nums mt-1">
                    {measuredDistanceM !== null ? measuredDistanceM.toFixed(4) : '---'}
                    <span className="text-xs font-normal text-slate-400 ml-1.5">m</span>
                  </div>
                  <div className="text-xs font-mono text-slate-500 tabular-nums mt-1">
                    液位 {liquidLevelM !== null ? `${(liquidLevelM * 1000).toFixed(0)} mm` : '---'} ·
                    偏差{' '}
                    {measuredDistanceM !== null
                      ? `${((measuredDistanceM - snapshot.trueDistanceM) * 1000).toFixed(2)} mm`
                      : '---'}
                  </div>
                </div>

                <div className="bg-white border border-slate-200 rounded-lg p-4">
                  <div className="text-xs text-slate-500">CZT 精细差拍频率</div>
                  <div className="text-2xl font-mono font-semibold text-slate-900 tabular-nums mt-1">
                    {result.czt && result.cztStatus === R120CztStatus.Ok
                      ? (result.czt.result.refinedSignedBeatHz / 1000).toFixed(4)
                      : result.fft
                        ? (result.fft.result.signedBeatHz / 1000).toFixed(3)
                        : '---'}
                    <span className="text-xs font-normal text-slate-400 ml-1.5">kHz</span>
                  </div>
                  <div className="text-xs font-mono text-slate-500 tabular-nums mt-1">
                    {cztDeltaHz !== null
                      ? `CZT 修正 ${cztDeltaHz >= 0 ? '+' : ''}${cztDeltaHz.toFixed(2)} Hz`
                      : `理论 ${(expectedTargetBeatHz / 1000).toFixed(3)} kHz`}
                  </div>
                </div>

                <div className="bg-white border border-slate-200 rounded-lg p-4">
                  <div className="text-xs text-slate-500">DMA 帧计数与首样</div>
                  <div className="text-2xl font-mono font-semibold text-slate-900 tabular-nums mt-1">
                    #{snapshot.frameCount}
                    <span className="text-xs font-normal text-slate-400 ml-1.5">
                      {snapshot.computeMs.toFixed(1)} ms
                    </span>
                  </div>
                  <div className="text-xs font-mono text-slate-500 tabular-nums mt-1">
                    I₀={snapshot.firstI} · Q₀={snapshot.firstQ} · {formatHex32(snapshot.lastRaw)}
                  </div>
                </div>

                <div className="bg-white border border-slate-200 rounded-lg p-4">
                  <div className="flex items-center justify-between text-xs">
                    <span className="text-slate-500">解算状态与质量</span>
                    <span className={`font-mono font-semibold ${statusTone}`}>
                      {statusGlyph} {STATUS_LABEL_ZH[result.status]}
                    </span>
                  </div>
                  <div className="text-2xl font-mono font-semibold text-slate-900 tabular-nums mt-1">
                    {result.fft ? result.fft.result.peakContrastDb.toFixed(1) : '---'}
                    <span className="text-xs font-normal text-slate-400 ml-1.5">dB</span>
                  </div>
                  <div className="text-xs font-mono text-slate-500 tabular-nums mt-1">
                    相干度{' '}
                    {result.phase ? `${(result.phase.coherentFraction * 100).toFixed(1)}%` : '---'} ·
                    φ {result.phase ? `${result.phase.phaseRad.toFixed(2)} rad` : '---'}
                  </div>
                </div>
              </div>

              {/* Live Dual-ADC Oscilloscope / FFT / CZT */}
              <SpectrumCharts
                packed={packed}
                sampleRateHz={measCfg.sampleRateHz}
                rangeConfig={rangeCfg}
                result={result}
                expectedTargetBeatHz={expectedTargetBeatHz}
              />

              {/* Real-Time Distance Trend Chart */}
              <DistanceTrendChart
                history={history}
                tankHeightM={tankHeightM}
                onClearHistory={() => setHistory([])}
                onExportCsv={handleExportCsv}
              />

              {/* Bottom DMA Watch & Pipeline Diagnostics + OLED Mirror */}
              <div className="grid grid-cols-1 xl:grid-cols-[1fr_310px] gap-4">
                <div className="bg-white border border-slate-200 rounded-lg overflow-hidden">
                  <div className="flex flex-wrap items-center justify-between gap-2 px-4 py-2.5 border-b border-slate-200 bg-slate-50/70">
                    <h2 className="text-xs font-semibold text-slate-800">
                      实时采集解算流水线 (r120_acq.c → r120_measure.c)
                    </h2>
                    <span className="text-xs font-mono text-slate-500 tabular-nums">
                      {snapshot.acqStateLabel} · DC I={result.fft ? result.fft.result.adcMeanI.toFixed(1) : '---'} / Q=
                      {result.fft ? result.fft.result.adcMeanQ.toFixed(1) : '---'}
                    </span>
                  </div>
                  <div className="divide-y divide-slate-100 text-xs">
                    {pipelineStages.map((stage) => (
                      <div
                        key={stage.step}
                        className="px-4 py-2 flex items-center justify-between gap-4"
                      >
                        <div className="flex items-center gap-2.5 min-w-0">
                          <span className="font-mono text-slate-400 tabular-nums">{stage.step}.</span>
                          <span className="font-medium text-slate-800 whitespace-nowrap">
                            {stage.name}
                          </span>
                          <span className="font-mono text-slate-500 tabular-nums truncate">
                            {stage.detail}
                          </span>
                        </div>
                        <span
                          className={`font-mono font-semibold whitespace-nowrap shrink-0 ${
                            stage.ok ? 'text-emerald-700' : 'text-rose-700'
                          }`}
                        >
                          {stage.ok ? '● 通过' : '✖ 拦截'}
                        </span>
                      </div>
                    ))}
                  </div>
                </div>

                <OledPreview
                  result={result}
                  tankHeightM={tankHeightM}
                  temperatureC={temperatureC}
                  frameCount={snapshot.frameCount}
                  isStreaming={isStreaming}
                />
              </div>
            </section>
          </div>
        )}

        {activeView === 'adf4159' && (
          <Adf4159PlannerView onApplyToPipeline={handleApplyAdfPlan} powerStage={powerStage} />
        )}

        {activeView === 'peripherals' && (
          <PeripheralsView
            powerStage={powerStage}
            onAdvancePower={() => setPowerStage((s) => Math.min(5, s + 1))}
            onSafeOffPower={() => setPowerStage(0)}
            temperatureC={temperatureC}
            onChangeTemperatureC={setTemperatureC}
            slantDistanceM={measuredDistanceM ?? snapshot.trueDistanceM}
            tankHeightM={tankHeightM}
          />
        )}

        {activeView === 'selftest' && <SelfTestView />}
      </main>
    </div>
  );
}

export default App;
