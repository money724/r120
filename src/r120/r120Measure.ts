// Port of User/r120_measure.c & User/r120_measure.h
// End-to-end single-frame pipeline: Clip Check -> 4096-pt FFT -> 129-pt CZT -> Range -> Phase Coherence.

import {
  R120_SIG_MAX_SAMPLES,
  R120SigAnalysis,
  R120SigStatus,
  analyzePackedSignal,
  clamp12,
} from './r120Signal';
import { R120CztAnalysis, R120CztStatus, refinePackedCzt } from './r120Czt';
import {
  R120RangeConfig,
  R120RangeResult,
  R120RangeStatus,
  expectedBeatHz,
  isRangeConfigValid,
  makeRangeConfig,
  rangeFromBeatHz,
} from './r120Range';
import {
  R120PhaseResult,
  R120PhaseStatus,
  estimatePackedPhase,
  wrappedDeltaRad,
} from './r120Phase';

export enum R120MeasStatus {
  Ok = 0,
  BadArgument = 1,
  BadConfig = 2,
  BadSamples = 3,
  Clipped = 4,
  NoTarget = 5,
  CztFailed = 6,
  RangeFailed = 7,
  LowQuality = 8,
  SelfTestFailed = 9,
}

export const R120_MEAS_STATUS_LABEL: Record<R120MeasStatus, string> = {
  [R120MeasStatus.Ok]: 'Nominal',
  [R120MeasStatus.BadArgument]: 'Bad Argument',
  [R120MeasStatus.BadConfig]: 'Bad Config',
  [R120MeasStatus.BadSamples]: 'Bad Samples',
  [R120MeasStatus.Clipped]: 'ADC Clipped',
  [R120MeasStatus.NoTarget]: 'No Target',
  [R120MeasStatus.CztFailed]: 'CZT Failed',
  [R120MeasStatus.RangeFailed]: 'Out of Range',
  [R120MeasStatus.LowQuality]: 'Low Coherence',
  [R120MeasStatus.SelfTestFailed]: 'Self-Test Failed',
};

export interface R120MeasConfig {
  range: R120RangeConfig;
  sampleRateHz: number;
  sampleCount: number;
  minPeakContrastDb: number;
  minAmplitudeCounts: number;
  minCoherentFraction: number;
  maxClippedFraction: number;
}

export interface R120MeasFullResult {
  status: R120MeasStatus;
  fftStatus: R120SigStatus;
  cztStatus: R120CztStatus;
  phaseStatus: R120PhaseStatus;
  rangeStatus: R120RangeStatus;
  clippedSamples: number;
  clippedFraction: number;
  distanceValid: boolean;
  phaseValid: boolean;
  fft: R120SigAnalysis | null;
  czt: R120CztAnalysis | null;
  phase: R120PhaseResult | null;
  range: R120RangeResult | null;
}

export interface SyntheticFrameParams {
  targetDistanceM: number;
  targetAmplitudeCounts: number;
  initialPhaseRad: number;
  noiseStdCounts: number;
  iqPolarity: 1 | -1;
  dcBiasI: number;
  dcBiasQ: number;
  enableClutter: boolean;
  clutterDistanceM: number;
  clutterAmplitudeCounts: number;
  seed: number;
}

function isMeasureConfigValid(cfg: R120MeasConfig | null | undefined): boolean {
  if (!cfg) return false;
  if (
    cfg.sampleCount < 32 ||
    cfg.sampleCount > R120_SIG_MAX_SAMPLES ||
    cfg.sampleRateHz < 1000 ||
    cfg.sampleRateHz > 1_000_000 ||
    !Number.isFinite(cfg.minPeakContrastDb) ||
    cfg.minPeakContrastDb < 0 ||
    cfg.minPeakContrastDb > 120 ||
    !Number.isFinite(cfg.minAmplitudeCounts) ||
    cfg.minAmplitudeCounts < 0 ||
    cfg.minAmplitudeCounts > 4095 ||
    !Number.isFinite(cfg.minCoherentFraction) ||
    cfg.minCoherentFraction < 0 ||
    cfg.minCoherentFraction > 1 ||
    !Number.isFinite(cfg.maxClippedFraction) ||
    cfg.maxClippedFraction < 0 ||
    cfg.maxClippedFraction > 1
  ) {
    return false;
  }
  return isRangeConfigValid(cfg.range);
}

export function makeMeasureConfig(
  range: R120RangeConfig,
  sampleRateHz: number,
  sampleCount: number
): { status: R120MeasStatus; config: R120MeasConfig } {
  const config: R120MeasConfig = {
    range: { ...range },
    sampleRateHz,
    sampleCount,
    minPeakContrastDb: 18.0,
    minAmplitudeCounts: 20.0,
    minCoherentFraction: 0.7,
    maxClippedFraction: 0.005, // 0.5%
  };
  return {
    status: isMeasureConfigValid(config) ? R120MeasStatus.Ok : R120MeasStatus.BadConfig,
    config,
  };
}

export function analyzePackedFrame(
  packed: Uint32Array,
  sampleCount: number,
  cfg: R120MeasConfig
): R120MeasFullResult {
  const out: R120MeasFullResult = {
    status: R120MeasStatus.BadArgument,
    fftStatus: R120SigStatus.BadArgument,
    cztStatus: R120CztStatus.BadArgument,
    phaseStatus: R120PhaseStatus.BadArgument,
    rangeStatus: R120RangeStatus.BadArgument,
    clippedSamples: 0,
    clippedFraction: 0,
    distanceValid: false,
    phaseValid: false,
    fft: null,
    czt: null,
    phase: null,
    range: null,
  };

  if (!packed || !cfg) return out;
  if (!isMeasureConfigValid(cfg)) {
    out.status = R120MeasStatus.BadConfig;
    return out;
  }
  if (sampleCount !== cfg.sampleCount || sampleCount > packed.length) {
    out.status = R120MeasStatus.BadSamples;
    return out;
  }

  for (let n = 0; n < sampleCount; ++n) {
    const raw = packed[n];
    const i = raw & 0x0fff;
    const q = (raw >>> 16) & 0x0fff;
    if ((raw & 0xf000f000) !== 0) {
      out.status = R120MeasStatus.BadSamples;
      return out;
    }
    if (i <= 1 || i >= 4094 || q <= 1 || q >= 4094) {
      ++out.clippedSamples;
    }
  }
  out.clippedFraction = out.clippedSamples / sampleCount;

  // Still compute FFT for visual inspection even if clipped, then return clipped status
  const fft = analyzePackedSignal(packed, sampleCount, cfg.sampleRateHz);
  out.fft = fft;
  out.fftStatus = fft.status;

  if (out.clippedFraction > cfg.maxClippedFraction) {
    out.status = R120MeasStatus.Clipped;
    return out;
  }

  if (fft.status !== R120SigStatus.Ok) {
    out.status = R120MeasStatus.NoTarget;
    return out;
  }

  if (
    !Number.isFinite(fft.result.peakContrastDb) ||
    fft.result.peakContrastDb < cfg.minPeakContrastDb ||
    !Number.isFinite(fft.result.amplitudeCounts) ||
    fft.result.amplitudeCounts < cfg.minAmplitudeCounts
  ) {
    out.status = R120MeasStatus.NoTarget;
    return out;
  }

  const czt = refinePackedCzt(packed, sampleCount, cfg.sampleRateHz, fft.status, fft.result);
  out.czt = czt;
  out.cztStatus = czt.status;
  if (czt.status !== R120CztStatus.Ok) {
    out.status = R120MeasStatus.CztFailed;
    return out;
  }

  const range = rangeFromBeatHz(czt.result.refinedSignedBeatHz, cfg.range);
  out.range = range.result;
  out.rangeStatus = range.status;
  if (range.status !== R120RangeStatus.Ok) {
    out.status = R120MeasStatus.RangeFailed;
    return out;
  }
  out.distanceValid = true;

  const phase = estimatePackedPhase(
    packed,
    sampleCount,
    cfg.sampleRateHz,
    czt.result.refinedSignedBeatHz
  );
  out.phase = phase.result;
  out.phaseStatus = phase.status;

  if (
    phase.status !== R120PhaseStatus.Ok ||
    !Number.isFinite(phase.result.coherentFraction) ||
    phase.result.coherentFraction < cfg.minCoherentFraction
  ) {
    out.phaseValid = false;
    out.status = R120MeasStatus.LowQuality;
    return out;
  }

  out.phaseValid = true;
  out.status = R120MeasStatus.Ok;
  return out;
}

// Deterministic Mulberry32 PRNG + Box-Muller Gaussian noise for repeatable frames
function makeGaussianRng(seed: number): () => number {
  let a = seed >>> 0;
  const rand = (): number => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
  let spare: number | null = null;
  return (): number => {
    if (spare !== null) {
      const val = spare;
      spare = null;
      return val;
    }
    let u = 0;
    let v = 0;
    while (u <= 1e-12) u = rand();
    while (v <= 1e-12) v = rand();
    const mag = Math.sqrt(-2.0 * Math.log(u));
    spare = mag * Math.sin(2.0 * Math.PI * v);
    return mag * Math.cos(2.0 * Math.PI * v);
  };
}

export function synthesizePackedFrame(
  cfg: R120MeasConfig,
  params: SyntheticFrameParams
): { packed: Uint32Array; expectedTargetBeatHz: number; expectedClutterBeatHz: number } {
  const packed = new Uint32Array(cfg.sampleCount);
  const rawDist = Math.max(0, params.targetDistanceM - cfg.range.distanceOffsetM);
  const beatMain = expectedBeatHz(rawDist, cfg.range).beatHz * params.iqPolarity;
  const beatClutter = params.enableClutter
    ? expectedBeatHz(
        Math.max(0, params.clutterDistanceM - cfg.range.distanceOffsetM),
        cfg.range
      ).beatHz * params.iqPolarity
    : 0;

  const gaussian = makeGaussianRng(params.seed);

  for (let n = 0; n < cfg.sampleCount; ++n) {
    const t = n / cfg.sampleRateHz;
    const phaseMain = 2.0 * Math.PI * beatMain * t + params.initialPhaseRad;
    let iVal = params.dcBiasI + params.targetAmplitudeCounts * Math.cos(phaseMain);
    let qVal = params.dcBiasQ + params.targetAmplitudeCounts * Math.sin(phaseMain);

    if (params.enableClutter && params.clutterAmplitudeCounts > 0) {
      const phaseClutter = 2.0 * Math.PI * beatClutter * t + 1.35;
      iVal += params.clutterAmplitudeCounts * Math.cos(phaseClutter);
      qVal += params.clutterAmplitudeCounts * Math.sin(phaseClutter);
    }

    if (params.noiseStdCounts > 0) {
      iVal += gaussian() * params.noiseStdCounts;
      qVal += gaussian() * params.noiseStdCounts;
    }

    const i12 = clamp12(iVal);
    const q12 = clamp12(qVal);
    packed[n] = (i12 | (q12 << 16)) >>> 0;
  }

  return {
    packed,
    expectedTargetBeatHz: beatMain,
    expectedClutterBeatHz: beatClutter,
  };
}

export function runMeasureSelfTest(): R120MeasFullResult {
  const { status: rangeStatus, config: range } = makeRangeConfig(
    5_000_000_000,
    6000,
    0.0,
    5.2,
    0.0
  );
  const { status: measCfgStatus, config: cfg } = makeMeasureConfig(range, 400_000, 2400);
  const { status: expStatus, beatHz: fHz } = expectedBeatHz(5.0, range);

  if (
    rangeStatus !== R120RangeStatus.Ok ||
    measCfgStatus !== R120MeasStatus.Ok ||
    expStatus !== R120RangeStatus.Ok
  ) {
    return {
      status: R120MeasStatus.SelfTestFailed,
      fftStatus: R120SigStatus.BadArgument,
      cztStatus: R120CztStatus.BadArgument,
      phaseStatus: R120PhaseStatus.BadArgument,
      rangeStatus: R120RangeStatus.BadArgument,
      clippedSamples: 0,
      clippedFraction: 0,
      distanceValid: false,
      phaseValid: false,
      fft: null,
      czt: null,
      phase: null,
      range: null,
    };
  }

  const selftestRaw = new Uint32Array(2400);
  for (let n = 0; n < 2400; ++n) {
    const phase = (2.0 * Math.PI * fHz * n) / 400_000.0 + 0.7;
    const i = clamp12(2048.0 + 680.0 * Math.cos(phase));
    const q = clamp12(2030.0 + 680.0 * Math.sin(phase));
    selftestRaw[n] = (i | (q << 16)) >>> 0;
  }

  const result = analyzePackedFrame(selftestRaw, 2400, cfg);
  if (
    result.status !== R120MeasStatus.Ok ||
    !result.range ||
    !result.phase ||
    Math.abs(result.range.distanceM - 5.0) > 0.015 ||
    Math.abs(wrappedDeltaRad(result.phase.phaseRad, 0.7)) > 0.05 ||
    result.phase.coherentFraction < 0.9
  ) {
    result.status = R120MeasStatus.SelfTestFailed;
  }
  return result;
}
