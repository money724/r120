// Port of User/r120_range.c & User/r120_range.h
// Monostatic FMCW beat-frequency to slant-distance conversion.

import { ADF4159_CFG_VALID_TAG, Adf4159CfgMode, Adf4159CfgPlan } from './adf4159Config';
import { R120SigResult, R120SigStatus, runSignalSelfTest } from './r120Signal';

export const R120_RANGE_LIGHT_SPEED_MPS = 299_792_458.0;
const R120_RANGE_MAX_BW_HZ = 10_000_000_000;
const R120_RANGE_MIN_US = 100;
const R120_RANGE_MAX_US = 1_000_000;

export enum R120RangeStatus {
  Ok = 0,
  BadArgument = 1,
  BadSweep = 2,
  NoValidPeak = 3,
  OutOfRange = 4,
  SelfTestFailed = 5,
}

export interface R120RangeConfig {
  bandwidthHz: number;
  durationUs: number;
  minDistanceM: number;
  maxDistanceM: number;
  distanceOffsetM: number;
}

export interface R120RangeResult {
  signedBeatHz: number;
  absBeatHz: number;
  slopeHzPerS: number;
  rawDistanceM: number;
  distanceM: number;
}

export function isRangeConfigValid(cfg: R120RangeConfig | null | undefined): boolean {
  if (!cfg) return false;
  return !(
    cfg.bandwidthHz <= 0 ||
    cfg.bandwidthHz > R120_RANGE_MAX_BW_HZ ||
    cfg.durationUs < R120_RANGE_MIN_US ||
    cfg.durationUs > R120_RANGE_MAX_US ||
    !Number.isFinite(cfg.minDistanceM) ||
    !Number.isFinite(cfg.maxDistanceM) ||
    !Number.isFinite(cfg.distanceOffsetM) ||
    cfg.minDistanceM < 0 ||
    cfg.maxDistanceM <= cfg.minDistanceM ||
    cfg.maxDistanceM > 10_000 ||
    Math.abs(cfg.distanceOffsetM) > 10_000
  );
}

export function makeRangeConfig(
  bandwidthHz: number,
  durationUs: number,
  minDistanceM: number,
  maxDistanceM: number,
  distanceOffsetM: number
): { status: R120RangeStatus; config: R120RangeConfig } {
  const candidate: R120RangeConfig = {
    bandwidthHz,
    durationUs,
    minDistanceM,
    maxDistanceM,
    distanceOffsetM,
  };
  if (!isRangeConfigValid(candidate)) {
    return { status: R120RangeStatus.BadSweep, config: candidate };
  }
  return { status: R120RangeStatus.Ok, config: candidate };
}

export function rangeConfigFromPlan(
  plan: Adf4159CfgPlan,
  minDistanceM: number,
  maxDistanceM: number,
  distanceOffsetM: number
): { status: R120RangeStatus; config: R120RangeConfig } {
  if (
    !plan ||
    plan.validTag !== ADF4159_CFG_VALID_TAG ||
    plan.mode !== Adf4159CfgMode.SingleSawtooth ||
    plan.steps < 2 ||
    plan.actualBandwidthHz === 0n ||
    plan.actualDurationUs === 0
  ) {
    return {
      status: R120RangeStatus.BadSweep,
      config: {
        bandwidthHz: 0,
        durationUs: 0,
        minDistanceM,
        maxDistanceM,
        distanceOffsetM,
      },
    };
  }

  return makeRangeConfig(
    Number(plan.actualBandwidthHz),
    plan.actualDurationUs,
    minDistanceM,
    maxDistanceM,
    distanceOffsetM
  );
}

export function rangeFromBeatHz(
  signedBeatHz: number,
  cfg: R120RangeConfig
): { status: R120RangeStatus; result: R120RangeResult } {
  const empty: R120RangeResult = {
    signedBeatHz: 0,
    absBeatHz: 0,
    slopeHzPerS: 0,
    rawDistanceM: 0,
    distanceM: 0,
  };

  if (!isRangeConfigValid(cfg)) {
    return { status: R120RangeStatus.BadSweep, result: empty };
  }
  if (!Number.isFinite(signedBeatHz) || signedBeatHz === 0) {
    return { status: R120RangeStatus.NoValidPeak, result: empty };
  }

  const fb = Math.abs(signedBeatHz);
  const slope = (cfg.bandwidthHz * 1_000_000.0) / cfg.durationUs;
  const raw = (R120_RANGE_LIGHT_SPEED_MPS * fb) / (2.0 * slope);
  const distanceM = raw + cfg.distanceOffsetM;

  const result: R120RangeResult = {
    signedBeatHz,
    absBeatHz: fb,
    slopeHzPerS: slope,
    rawDistanceM: raw,
    distanceM,
  };

  if (
    !Number.isFinite(distanceM) ||
    distanceM < cfg.minDistanceM ||
    distanceM > cfg.maxDistanceM
  ) {
    return { status: R120RangeStatus.OutOfRange, result };
  }
  return { status: R120RangeStatus.Ok, result };
}

export function rangeFromSignal(
  signalStatus: R120SigStatus,
  signal: R120SigResult,
  cfg: R120RangeConfig
): { status: R120RangeStatus; result: R120RangeResult } {
  const empty: R120RangeResult = {
    signedBeatHz: 0,
    absBeatHz: 0,
    slopeHzPerS: 0,
    rawDistanceM: 0,
    distanceM: 0,
  };

  if (
    signalStatus !== R120SigStatus.Ok ||
    !signal ||
    signal.sampleCount < 32 ||
    signal.sampleRateHz === 0 ||
    !Number.isFinite(signal.signedBeatHz) ||
    !Number.isFinite(signal.absBeatHz) ||
    Math.abs(Math.abs(signal.signedBeatHz) - signal.absBeatHz) > 1.0
  ) {
    return { status: R120RangeStatus.NoValidPeak, result: empty };
  }

  return rangeFromBeatHz(signal.signedBeatHz, cfg);
}

export function expectedBeatHz(
  rawDistanceM: number,
  cfg: R120RangeConfig
): { status: R120RangeStatus; beatHz: number } {
  if (!isRangeConfigValid(cfg)) {
    return { status: R120RangeStatus.BadSweep, beatHz: 0 };
  }
  if (!Number.isFinite(rawDistanceM) || rawDistanceM < 0) {
    return { status: R120RangeStatus.BadArgument, beatHz: 0 };
  }
  const slope = (cfg.bandwidthHz * 1_000_000.0) / cfg.durationUs;
  const beatHz = (2.0 * slope * rawDistanceM) / R120_RANGE_LIGHT_SPEED_MPS;
  return { status: R120RangeStatus.Ok, beatHz };
}

export function runRangeSelfTest(): { status: R120RangeStatus; result: R120RangeResult } {
  const { status: cfgStatus, config } = makeRangeConfig(5_000_000_000, 6000, 0.0, 5.0, 0.0);
  if (cfgStatus !== R120RangeStatus.Ok) {
    return {
      status: R120RangeStatus.SelfTestFailed,
      result: { signedBeatHz: 0, absBeatHz: 0, slopeHzPerS: 0, rawDistanceM: 0, distanceM: 0 },
    };
  }
  const sig = runSignalSelfTest();
  const range = rangeFromSignal(sig.status, sig.result, config);
  if (range.status !== R120RangeStatus.Ok || Math.abs(range.result.distanceM - 4.99654097) > 0.02) {
    return { status: R120RangeStatus.SelfTestFailed, result: range.result };
  }
  return { status: R120RangeStatus.Ok, result: range.result };
}
