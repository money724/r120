// Port of User/r120_phase.c & User/r120_phase.h
// Hann-weighted baseband complex demodulation and single-tone coherence metric.

import {
  R120_SIG_MAX_BEAT_HZ,
  R120_SIG_MAX_SAMPLES,
  R120_SIG_MIN_BEAT_HZ,
  R120SigStatus,
  analyzePackedSignal,
  clamp12,
} from './r120Signal';
import { R120CztStatus, refinePackedCzt } from './r120Czt';

const R120_PHASE_MIN_AMP_COUNTS = 5.0;
const R120_PHASE_MIN_COHERENCE = 0.2;
const R120_PHASE_TEST_FREQ_HZ = 27777.7778;
const R120_PHASE_TEST_RAD = 0.7;

export enum R120PhaseStatus {
  Ok = 0,
  BadArgument = 1,
  BadFrequency = 2,
  WeakSignal = 3,
  SelfTestFailed = 4,
}

export interface R120PhaseResult {
  sampleCount: number;
  sampleRateHz: number;
  adcMeanI: number;
  adcMeanQ: number;
  signedBeatHz: number;
  phaseRad: number;
  phaseDeg: number;
  amplitudeCounts: number;
  coherentFraction: number;
}

function wrapRad(rad: number): number {
  if (!Number.isFinite(rad)) return 0;
  let r = rad;
  while (r > Math.PI) r -= 2 * Math.PI;
  while (r < -Math.PI) r += 2 * Math.PI;
  return r;
}

export function wrappedDeltaRad(nowRad: number, previousRad: number): number {
  if (!Number.isFinite(nowRad) || !Number.isFinite(previousRad)) return Number.NaN;
  return wrapRad(nowRad - previousRad);
}

export function estimatePackedPhase(
  packed: Uint32Array,
  sampleCount: number,
  sampleRateHz: number,
  signedBeatHz: number
): { status: R120PhaseStatus; result: R120PhaseResult } {
  const out: R120PhaseResult = {
    sampleCount: 0,
    sampleRateHz: 0,
    adcMeanI: 0,
    adcMeanQ: 0,
    signedBeatHz: 0,
    phaseRad: 0,
    phaseDeg: 0,
    amplitudeCounts: 0,
    coherentFraction: 0,
  };

  if (
    !packed ||
    sampleCount < 32 ||
    sampleCount > R120_SIG_MAX_SAMPLES ||
    sampleCount > packed.length ||
    sampleRateHz < 1000 ||
    sampleRateHz > 1_000_000
  ) {
    return { status: R120PhaseStatus.BadArgument, result: out };
  }

  out.sampleCount = sampleCount;
  out.sampleRateHz = sampleRateHz;
  out.signedBeatHz = signedBeatHz;

  if (
    !Number.isFinite(signedBeatHz) ||
    Math.abs(signedBeatHz) < R120_SIG_MIN_BEAT_HZ ||
    Math.abs(signedBeatHz) > R120_SIG_MAX_BEAT_HZ ||
    Math.abs(signedBeatHz) >= 0.5 * sampleRateHz
  ) {
    return { status: R120PhaseStatus.BadFrequency, result: out };
  }

  let meanI = 0;
  let meanQ = 0;
  for (let n = 0; n < sampleCount; ++n) {
    const word = packed[n];
    meanI += word & 0x0fff;
    meanQ += (word >>> 16) & 0x0fff;
  }
  meanI /= sampleCount;
  meanQ /= sampleCount;
  out.adcMeanI = meanI;
  out.adcMeanQ = meanQ;

  const delta = (-2.0 * Math.PI * signedBeatHz) / sampleRateHz;
  const dRe = Math.cos(delta);
  const dIm = Math.sin(delta);
  let oscRe = 1.0;
  let oscIm = 0.0;

  let sumRe = 0;
  let sumIm = 0;
  let sumWin = 0;
  let sumWeightedPower = 0;

  for (let n = 0; n < sampleCount; ++n) {
    const word = packed[n];
    const i = (word & 0x0fff) - meanI;
    const q = ((word >>> 16) & 0x0fff) - meanQ;
    const windowPhase = (2.0 * Math.PI * n) / (sampleCount - 1);
    const win = 0.5 * (1.0 - Math.cos(windowPhase));

    sumRe += win * (i * oscRe - q * oscIm);
    sumIm += win * (i * oscIm + q * oscRe);
    sumWin += win;
    sumWeightedPower += win * (i * i + q * q);

    const nextRe = oscRe * dRe - oscIm * dIm;
    const nextIm = oscRe * dIm + oscIm * dRe;
    oscRe = nextRe;
    oscIm = nextIm;

    if ((n & 127) === 127) {
      const absoluteAngle = delta * (n + 1);
      oscRe = Math.cos(absoluteAngle);
      oscIm = Math.sin(absoluteAngle);
    }
  }

  if (sumWin <= 0 || sumWeightedPower <= 0) {
    return { status: R120PhaseStatus.WeakSignal, result: out };
  }

  const magSq = sumRe * sumRe + sumIm * sumIm;
  const amp = Math.sqrt(magSq) / sumWin;
  let coherent = magSq / (sumWin * sumWeightedPower);

  if (
    !Number.isFinite(amp) ||
    !Number.isFinite(coherent) ||
    amp < R120_PHASE_MIN_AMP_COUNTS ||
    coherent < R120_PHASE_MIN_COHERENCE
  ) {
    return { status: R120PhaseStatus.WeakSignal, result: out };
  }

  if (coherent > 1.0) coherent = 1.0;
  out.phaseRad = Math.atan2(sumIm, sumRe);
  out.phaseDeg = out.phaseRad * (180.0 / Math.PI);
  out.amplitudeCounts = amp;
  out.coherentFraction = coherent;
  return { status: R120PhaseStatus.Ok, result: out };
}

export function runPhaseSelfTest(): {
  status: R120PhaseStatus;
  result: R120PhaseResult;
  refinedBeatHz: number;
  phaseErrorRad: number;
} {
  const rateHz = 400_000;
  const amplitude = 680.0;
  const raw = new Uint32Array(R120_SIG_MAX_SAMPLES);

  for (let n = 0; n < R120_SIG_MAX_SAMPLES; ++n) {
    const angle = (2.0 * Math.PI * R120_PHASE_TEST_FREQ_HZ * n) / rateHz + R120_PHASE_TEST_RAD;
    const i = clamp12(2048.0 + amplitude * Math.cos(angle));
    const q = clamp12(2030.0 + amplitude * Math.sin(angle));
    raw[n] = (i | (q << 16)) >>> 0;
  }

  const fft = analyzePackedSignal(raw, R120_SIG_MAX_SAMPLES, rateHz);
  if (fft.status !== R120SigStatus.Ok) {
    return {
      status: R120PhaseStatus.SelfTestFailed,
      result: {
        sampleCount: 0,
        sampleRateHz: 0,
        adcMeanI: 0,
        adcMeanQ: 0,
        signedBeatHz: 0,
        phaseRad: 0,
        phaseDeg: 0,
        amplitudeCounts: 0,
        coherentFraction: 0,
      },
      refinedBeatHz: 0,
      phaseErrorRad: Number.NaN,
    };
  }

  const czt = refinePackedCzt(raw, R120_SIG_MAX_SAMPLES, rateHz, fft.status, fft.result);
  if (czt.status !== R120CztStatus.Ok) {
    return {
      status: R120PhaseStatus.SelfTestFailed,
      result: {
        sampleCount: 0,
        sampleRateHz: 0,
        adcMeanI: 0,
        adcMeanQ: 0,
        signedBeatHz: 0,
        phaseRad: 0,
        phaseDeg: 0,
        amplitudeCounts: 0,
        coherentFraction: 0,
      },
      refinedBeatHz: 0,
      phaseErrorRad: Number.NaN,
    };
  }

  const phase = estimatePackedPhase(
    raw,
    R120_SIG_MAX_SAMPLES,
    rateHz,
    czt.result.refinedSignedBeatHz
  );
  const phaseErrorRad = wrappedDeltaRad(phase.result.phaseRad, R120_PHASE_TEST_RAD);

  if (
    phase.status !== R120PhaseStatus.Ok ||
    Math.abs(czt.result.refinedSignedBeatHz - R120_PHASE_TEST_FREQ_HZ) > 2.0 ||
    Math.abs(phaseErrorRad) > 0.03 ||
    phase.result.coherentFraction < 0.95
  ) {
    return {
      status: R120PhaseStatus.SelfTestFailed,
      result: phase.result,
      refinedBeatHz: czt.result.refinedSignedBeatHz,
      phaseErrorRad,
    };
  }

  return {
    status: R120PhaseStatus.Ok,
    result: phase.result,
    refinedBeatHz: czt.result.refinedSignedBeatHz,
    phaseErrorRad,
  };
}
