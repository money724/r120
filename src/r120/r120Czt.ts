// Port of User/r120_czt.c & User/r120_czt.h
// 129-point direct local Chirp Z-Transform frequency refinement with quadratic power interpolation.

import {
  R120_SIG_FFT_SIZE,
  R120_SIG_MAX_BEAT_HZ,
  R120_SIG_MAX_SAMPLES,
  R120_SIG_MIN_BEAT_HZ,
  R120SigResult,
  R120SigStatus,
  analyzePackedSignal,
  clamp12,
} from './r120Signal';

export const R120_CZT_GRID_POINTS = 129;
export const R120_CZT_HALF_SPAN_BINS = 1.5;

const R120_CZT_MIN_AMPLITUDE = 5.0;
const R120_CZT_POWER_FLOOR = 1e-15;

export enum R120CztStatus {
  Ok = 0,
  BadArgument = 1,
  BadCoarseEstimate = 2,
  NoPeak = 3,
}

export interface R120CztResult {
  sampleCount: number;
  sampleRateHz: number;
  gridPoints: number;
  coarseSignedBeatHz: number;
  refinedSignedBeatHz: number;
  absBeatHz: number;
  gridStartHz: number;
  gridEndHz: number;
  gridSpacingHz: number;
  peakAmplitudeCounts: number;
  peakIndex: number;
}

export interface R120CztAnalysis {
  status: R120CztStatus;
  result: R120CztResult;
  gridFreqHz: Float64Array;
  gridPowerDb: Float64Array;
}

function powerAtHz(
  windowedI: Float64Array,
  windowedQ: Float64Array,
  freqHz: number,
  count: number,
  sampleRateHz: number
): number {
  const phaseStep = (-2.0 * Math.PI * freqHz) / sampleRateHz;
  const dRe = Math.cos(phaseStep);
  const dIm = Math.sin(phaseStep);
  let wRe = 1.0;
  let wIm = 0.0;
  let sumRe = 0.0;
  let sumIm = 0.0;

  for (let n = 0; n < count; ++n) {
    const i = windowedI[n];
    const q = windowedQ[n];
    sumRe += i * wRe - q * wIm;
    sumIm += i * wIm + q * wRe;

    const nextRe = wRe * dRe - wIm * dIm;
    const nextIm = wRe * dIm + wIm * dRe;
    wRe = nextRe;
    wIm = nextIm;

    if ((n & 255) === 255) {
      const phase = phaseStep * (n + 1);
      wRe = Math.cos(phase);
      wIm = Math.sin(phase);
    }
  }
  return sumRe * sumRe + sumIm * sumIm;
}

export function refinePackedCzt(
  packed: Uint32Array,
  sampleCount: number,
  sampleRateHz: number,
  coarseStatus: R120SigStatus,
  coarse: R120SigResult
): R120CztAnalysis {
  const emptyResult: R120CztResult = {
    sampleCount: 0,
    sampleRateHz: 0,
    gridPoints: 0,
    coarseSignedBeatHz: 0,
    refinedSignedBeatHz: 0,
    absBeatHz: 0,
    gridStartHz: 0,
    gridEndHz: 0,
    gridSpacingHz: 0,
    peakAmplitudeCounts: 0,
    peakIndex: 0,
  };
  const gridFreqHz = new Float64Array(R120_CZT_GRID_POINTS);
  const gridPowerDb = new Float64Array(R120_CZT_GRID_POINTS);

  if (
    !packed ||
    !coarse ||
    sampleCount < 32 ||
    sampleCount > R120_SIG_MAX_SAMPLES ||
    sampleCount > packed.length ||
    sampleRateHz < 1000 ||
    sampleRateHz > 1_000_000
  ) {
    return { status: R120CztStatus.BadArgument, result: emptyResult, gridFreqHz, gridPowerDb };
  }

  const coarseHz = coarse.signedBeatHz;
  if (
    coarseStatus !== R120SigStatus.Ok ||
    coarse.sampleCount !== sampleCount ||
    coarse.sampleRateHz !== sampleRateHz ||
    !Number.isFinite(coarseHz) ||
    Math.abs(coarseHz) < R120_SIG_MIN_BEAT_HZ ||
    Math.abs(coarseHz) > R120_SIG_MAX_BEAT_HZ ||
    !Number.isFinite(coarse.amplitudeCounts) ||
    coarse.amplitudeCounts < R120_CZT_MIN_AMPLITUDE
  ) {
    return {
      status: R120CztStatus.BadCoarseEstimate,
      result: emptyResult,
      gridFreqHz,
      gridPowerDb,
    };
  }

  const halfWidth = (R120_CZT_HALF_SPAN_BINS * sampleRateHz) / R120_SIG_FFT_SIZE;
  const minHz = coarseHz > 0 ? R120_SIG_MIN_BEAT_HZ : -R120_SIG_MAX_BEAT_HZ;
  const maxHz = coarseHz > 0 ? R120_SIG_MAX_BEAT_HZ : -R120_SIG_MIN_BEAT_HZ;

  const startHz = Math.max(coarseHz - halfWidth, minHz);
  const endHz = Math.min(coarseHz + halfWidth, maxHz);
  if (!Number.isFinite(startHz) || !Number.isFinite(endHz) || endHz <= startHz) {
    return {
      status: R120CztStatus.BadCoarseEstimate,
      result: emptyResult,
      gridFreqHz,
      gridPowerDb,
    };
  }

  const stepHz = (endHz - startHz) / (R120_CZT_GRID_POINTS - 1);

  let meanI = 0;
  let meanQ = 0;
  for (let n = 0; n < sampleCount; ++n) {
    const word = packed[n];
    meanI += word & 0x0fff;
    meanQ += (word >>> 16) & 0x0fff;
  }
  meanI /= sampleCount;
  meanQ /= sampleCount;

  const windowedI = new Float64Array(sampleCount);
  const windowedQ = new Float64Array(sampleCount);
  let sumWindow = 0;
  for (let n = 0; n < sampleCount; ++n) {
    const word = packed[n];
    const phase = (2.0 * Math.PI * n) / (sampleCount - 1);
    const win = 0.5 * (1.0 - Math.cos(phase));
    windowedI[n] = ((word & 0x0fff) - meanI) * win;
    windowedQ[n] = (((word >>> 16) & 0x0fff) - meanQ) * win;
    sumWindow += win;
  }
  if (sumWindow <= 0) {
    return { status: R120CztStatus.BadArgument, result: emptyResult, gridFreqHz, gridPowerDb };
  }

  const gridPower = new Float64Array(R120_CZT_GRID_POINTS);
  let maxPower = 0;
  let peakIndex = 0;
  const normSq = sumWindow * sumWindow;

  for (let k = 0; k < R120_CZT_GRID_POINTS; ++k) {
    const freqHz = k === R120_CZT_GRID_POINTS - 1 ? endHz : startHz + k * stepHz;
    const power = powerAtHz(windowedI, windowedQ, freqHz, sampleCount, sampleRateHz);
    gridFreqHz[k] = freqHz;
    gridPower[k] = power;
    gridPowerDb[k] = 10 * Math.log10(Math.max(power / normSq, 1e-16));
    if (power > maxPower) {
      maxPower = power;
      peakIndex = k;
    }
  }

  const out: R120CztResult = {
    sampleCount,
    sampleRateHz,
    gridPoints: R120_CZT_GRID_POINTS,
    coarseSignedBeatHz: coarseHz,
    refinedSignedBeatHz: 0,
    absBeatHz: 0,
    gridStartHz: startHz,
    gridEndHz: endHz,
    gridSpacingHz: stepHz,
    peakAmplitudeCounts: Math.sqrt(maxPower) / sumWindow,
    peakIndex,
  };

  if (
    maxPower <= R120_CZT_POWER_FLOOR ||
    out.peakAmplitudeCounts < R120_CZT_MIN_AMPLITUDE ||
    peakIndex === 0 ||
    peakIndex === R120_CZT_GRID_POINTS - 1
  ) {
    return { status: R120CztStatus.NoPeak, result: out, gridFreqHz, gridPowerDb };
  }

  let refinedHz = startHz + peakIndex * stepHz;
  const left = gridPower[peakIndex - 1];
  const center = gridPower[peakIndex];
  const right = gridPower[peakIndex + 1];
  const denom = left - 2.0 * center + right;
  if (Math.abs(denom) > 1e-5 * Math.max(center, 1.0)) {
    let offset = (0.5 * (left - right)) / denom;
    if (offset > 0.5) offset = 0.5;
    if (offset < -0.5) offset = -0.5;
    refinedHz += offset * stepHz;
  }

  out.refinedSignedBeatHz = refinedHz;
  out.absBeatHz = Math.abs(refinedHz);
  return { status: R120CztStatus.Ok, result: out, gridFreqHz, gridPowerDb };
}

export function runCztSelfTest(): R120CztAnalysis {
  const sampleRate = 400_000;
  const signalHz = 27777.7778;
  const amplitude = 680.0;
  const selftestRaw = new Uint32Array(R120_SIG_MAX_SAMPLES);

  for (let n = 0; n < R120_SIG_MAX_SAMPLES; ++n) {
    const phase = (2.0 * Math.PI * signalHz * n) / sampleRate;
    const i = clamp12(2048.0 + amplitude * Math.cos(phase));
    const q = clamp12(2030.0 + amplitude * Math.sin(phase));
    selftestRaw[n] = (i | (q << 16)) >>> 0;
  }

  const coarse = analyzePackedSignal(selftestRaw, R120_SIG_MAX_SAMPLES, sampleRate);
  return refinePackedCzt(
    selftestRaw,
    R120_SIG_MAX_SAMPLES,
    sampleRate,
    coarse.status,
    coarse.result
  );
}
