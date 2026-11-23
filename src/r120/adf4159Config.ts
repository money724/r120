// Port of User/adf4159_config.c & User/adf4159_config.h
// ADF4159 Rev.D register map, fractional modulus = 2^25, 75 MHz REFIN, TRX divider = 64.

export const ADF4159_CFG_INIT_WORD_COUNT = 11;
export const ADF4159_CFG_REF_HZ = 75_000_000n;
export const ADF4159_CFG_TRX_DIVIDER = 64n;
export const ADF4159_CFG_MIN_RF_HZ = 120_000_000_000n;
export const ADF4159_CFG_MAX_RF_HZ = 125_000_000_000n;
export const ADF4159_CFG_VALID_TAG = 0x415df159;

const CFG_MODULUS = 33_554_432n; // 2^25
const CFG_REF_TIMES_DIV = 4_800_000_000n; // 75 MHz * 64
const CFG_CLK1 = 180;
const CFG_CLK2 = 1;
const CFG_R0_MUX_DIG_LOCK = (6 << 27) >>> 0;
const CFG_R0_MUX_RAMP_DONE = (15 << 27) >>> 0;
const CFG_R0_RAMP_ON = (1 << 31) >>> 0;
const CFG_R3_FIXED = 0x000380c3;
const CFG_R3_SINGLE_SAW = 0x000388c3;
const CFG_R2_75MHZ = 0x0f0085a2;
const CFG_R4_FIXED_CLK1 = 0x00000084;
const CFG_R4_FIXED_CLK2 = 0x000000c4;
const CFG_R4_RAMP_CLK1 = 0x00780084;
const CFG_R4_RAMP_CLK2 = 0x007800c4;

export enum Adf4159CfgMode {
  Fixed = 1,
  SingleSawtooth = 2,
}

export enum Adf4159CfgStatus {
  Ok = 0,
  BadArg = 1,
  Unrepresentable = 2,
  NotReady = 3,
  WriteFailed = 4,
}

export interface Adf4159RegisterMeta {
  index: number;
  registerName: string;
  word: number;
  hex: string;
  description: string;
}

export interface Adf4159CfgPlan {
  validTag: number;
  mode: Adf4159CfgMode;
  requestedStartHz: bigint;
  requestedStopHz: bigint;
  actualStartHz: bigint;
  actualStopHz: bigint;
  actualBandwidthHz: bigint;
  requestedDurationUs: number;
  actualDurationUs: number;
  steps: number;
  clk1: number;
  clk2: number;
  deviation: number;
  deviationOffset: number;
  integerN: number;
  frac25: number;
  words: number[];
  r0RampOff: number;
  r0RampOn: number;
}

export interface Adf4159PlanOutcome {
  status: Adf4159CfgStatus;
  plan: Adf4159CfgPlan | null;
  error?: string;
}

function makeFrequencyWords(
  rfHz: bigint,
  muxBits: number
): { status: Adf4159CfgStatus; r0: number; r1: number; actualRfHz: bigint; integerN: number; frac: number } {
  if (rfHz < ADF4159_CFG_MIN_RF_HZ || rfHz > ADF4159_CFG_MAX_RF_HZ) {
    return { status: Adf4159CfgStatus.BadArg, r0: 0, r1: 0, actualRfHz: 0n, integerN: 0, frac: 0 };
  }

  let integerN = rfHz / CFG_REF_TIMES_DIV;
  const remainder = rfHz % CFG_REF_TIMES_DIV;
  let frac = (remainder * CFG_MODULUS + CFG_REF_TIMES_DIV / 2n) / CFG_REF_TIMES_DIV;

  if (frac >= CFG_MODULUS) {
    integerN += 1n;
    frac = 0n;
  }

  // R2 prescaler = 4/5 -> minimum INT = 23
  if (integerN < 23n || integerN > 4095n) {
    return { status: Adf4159CfgStatus.Unrepresentable, r0: 0, r1: 0, actualRfHz: 0n, integerN: 0, frac: 0 };
  }

  const intNum = Number(integerN);
  const fracNum = Number(frac);

  const r1 = ((((fracNum & 0x1fff) << 15) >>> 0) | 0x1) >>> 0;
  const r0 = (muxBits | ((intNum << 15) >>> 0) | (((fracNum >>> 13) << 3) >>> 0)) >>> 0;

  const effectiveNum = frac * CFG_REF_TIMES_DIV;
  const actualRfHz = integerN * CFG_REF_TIMES_DIV + (effectiveNum + CFG_MODULUS / 2n) / CFG_MODULUS;

  return { status: Adf4159CfgStatus.Ok, r0, r1, actualRfHz, integerN: intNum, frac: fracNum };
}

function actualBandwidth(dev: number, offset: number, steps: number): bigint {
  const k = BigInt(dev) * (1n << BigInt(offset)) * BigInt(steps);
  const q = k / CFG_MODULUS;
  const r = k % CFG_MODULUS;
  return q * CFG_REF_TIMES_DIV + (r * CFG_REF_TIMES_DIV + CFG_MODULUS / 2n) / CFG_MODULUS;
}

export function planFixedHz(rfHz: bigint): Adf4159PlanOutcome {
  const freq = makeFrequencyWords(rfHz, CFG_R0_MUX_DIG_LOCK);
  if (freq.status !== Adf4159CfgStatus.Ok) {
    return {
      status: freq.status,
      plan: null,
      error:
        freq.status === Adf4159CfgStatus.BadArg
          ? 'RF frequency must be between 120.000 GHz and 125.000 GHz.'
          : 'Frequency cannot be represented with prescaler 4/5 (INT 23..4095).',
    };
  }

  const words = [
    0x00000007, // [0] R7
    0x00000006, // [1] R6 step1 unused
    0x00800006, // [2] R6 step2 zero
    0x00000005, // [3] R5 DEV1 unused
    0x00800005, // [4] R5 DEV2 zero
    CFG_R4_FIXED_CLK1, // [5] R4 CLK1
    CFG_R4_FIXED_CLK2, // [6] R4 CLK2
    CFG_R3_FIXED, // [7] R3
    CFG_R2_75MHZ, // [8] R2
    freq.r1, // [9] R1
    freq.r0, // [10] R0
  ];

  return {
    status: Adf4159CfgStatus.Ok,
    plan: {
      validTag: ADF4159_CFG_VALID_TAG,
      mode: Adf4159CfgMode.Fixed,
      requestedStartHz: rfHz,
      requestedStopHz: rfHz,
      actualStartHz: freq.actualRfHz,
      actualStopHz: freq.actualRfHz,
      actualBandwidthHz: 0n,
      requestedDurationUs: 0,
      actualDurationUs: 0,
      steps: 0,
      clk1: CFG_CLK1,
      clk2: CFG_CLK2,
      deviation: 0,
      deviationOffset: 0,
      integerN: freq.integerN,
      frac25: freq.frac,
      words,
      r0RampOff: freq.r0,
      r0RampOn: freq.r0,
    },
  };
}

export function planRampHz(startHz: bigint, stopHz: bigint, durationUs: number): Adf4159PlanOutcome {
  if (
    startHz < ADF4159_CFG_MIN_RF_HZ ||
    stopHz > ADF4159_CFG_MAX_RF_HZ ||
    stopHz <= startHz ||
    !Number.isFinite(durationUs) ||
    durationUs < 1000 ||
    durationUs > 100_000
  ) {
    return {
      status: Adf4159CfgStatus.BadArg,
      plan: null,
      error: 'Valid ramp bounds: 120–125 GHz, Stop > Start, Duration 1,000–100,000 µs.',
    };
  }

  const durInt = Math.round(durationUs);
  const stepsBig = (BigInt(durInt) * 75n + BigInt(CFG_CLK1) / 2n) / BigInt(CFG_CLK1);
  if (stepsBig < 2n || stepsBig > 0xfffffn) {
    return {
      status: Adf4159CfgStatus.Unrepresentable,
      plan: null,
      error: 'Step count falls outside 20-bit R6 register limits.',
    };
  }
  const steps = Number(stepsBig);

  const bw = stopHz - startHz;
  const num = bw * CFG_MODULUS;
  const denominator = CFG_REF_TIMES_DIV * stepsBig;

  let dev = 0;
  let chosenOffset = 0;
  for (let offset = 0; offset <= 15; ++offset) {
    const scaled = denominator * (1n << BigInt(offset));
    const candidate = (num + scaled / 2n) / scaled;
    if (candidate >= 1n && candidate <= 32767n) {
      dev = Number(candidate);
      chosenOffset = offset;
      break;
    }
  }
  if (dev === 0) {
    return {
      status: Adf4159CfgStatus.Unrepresentable,
      plan: null,
      error: 'Cannot fit deviation into 15-bit signed DEV word with offsets 0..15.',
    };
  }

  const freq = makeFrequencyWords(startHz, CFG_R0_MUX_RAMP_DONE);
  if (freq.status !== Adf4159CfgStatus.Ok) {
    return { status: freq.status, plan: null, error: 'Start frequency unrepresentable.' };
  }

  const realBw = actualBandwidth(dev, chosenOffset, steps);
  if (freq.actualRfHz + realBw > ADF4159_CFG_MAX_RF_HZ) {
    return {
      status: Adf4159CfgStatus.Unrepresentable,
      plan: null,
      error: 'Quantized stop frequency exceeds 125.000 GHz software envelope.',
    };
  }

  const r6Step1 = ((((steps & 0xfffff) << 3) >>> 0) | 0x6) >>> 0;
  const r5Dev1 = ((((chosenOffset << 19) >>> 0) | ((dev << 3) >>> 0)) | 0x5) >>> 0;

  const words = [
    0x00000007, // [0] R7
    r6Step1, // [1] R6 Step 1
    0x00800006, // [2] R6 Step 2 (zero)
    r5Dev1, // [3] R5 Dev 1
    0x00800005, // [4] R5 Dev 2 (zero)
    CFG_R4_RAMP_CLK1, // [5] R4 CLK1
    CFG_R4_RAMP_CLK2, // [6] R4 CLK2
    CFG_R3_SINGLE_SAW, // [7] R3
    CFG_R2_75MHZ, // [8] R2
    freq.r1, // [9] R1
    freq.r0, // [10] R0 (ramp off, armed)
  ];

  const actualDurationUs = Number((stepsBig * BigInt(CFG_CLK1) + 37n) / 75n);

  return {
    status: Adf4159CfgStatus.Ok,
    plan: {
      validTag: ADF4159_CFG_VALID_TAG,
      mode: Adf4159CfgMode.SingleSawtooth,
      requestedStartHz: startHz,
      requestedStopHz: stopHz,
      actualStartHz: freq.actualRfHz,
      actualStopHz: freq.actualRfHz + realBw,
      actualBandwidthHz: realBw,
      requestedDurationUs: durInt,
      actualDurationUs,
      steps,
      clk1: CFG_CLK1,
      clk2: CFG_CLK2,
      deviation: dev,
      deviationOffset: chosenOffset,
      integerN: freq.integerN,
      frac25: freq.frac,
      words,
      r0RampOff: freq.r0,
      r0RampOn: (freq.r0 | CFG_R0_RAMP_ON) >>> 0,
    },
  };
}

export function formatHex32(word: number): string {
  return '0x' + (word >>> 0).toString(16).toUpperCase().padStart(8, '0');
}

export function describePlanRegisters(plan: Adf4159CfgPlan): Adf4159RegisterMeta[] {
  const isRamp = plan.mode === Adf4159CfgMode.SingleSawtooth;
  const meta: Array<{ name: string; desc: string }> = [
    { name: 'R7', desc: 'TXDATA & interrupt triggers disabled' },
    {
      name: 'R6 (Step 1)',
      desc: isRamp ? `Up-ramp step count = ${plan.steps} (20-bit)` : 'Unused in fixed-frequency mode',
    },
    { name: 'R6 (Step 2)', desc: 'Down-ramp step count = 0 (StepSel = 1)' },
    {
      name: 'R5 (Dev 1)',
      desc: isRamp
        ? `DEV = ${plan.deviation}, Offset = 2^${plan.deviationOffset}`
        : 'Unused in fixed-frequency mode',
    },
    { name: 'R5 (Dev 2)', desc: 'Second deviation word = 0 (DevSel = 1)' },
    {
      name: 'R4 (CLK1)',
      desc: isRamp ? 'Ramp clock mode enabled, CLK2=1 (CLK1 path)' : 'Fixed mode, clock divider off',
    },
    {
      name: 'R4 (CLK2)',
      desc: isRamp ? 'Ramp clock mode enabled, CLK2=1 (CLK2 path)' : 'Fixed mode, CLK2 selector',
    },
    {
      name: 'R3',
      desc: isRamp ? 'Single sawtooth burst, negative bleed, N-sel' : 'Fixed CW output, ramp disabled',
    },
    { name: 'R2', desc: '75 MHz REFIN, R=1, CP=5.0 mA, prescaler 4/5, CLK1=180' },
    {
      name: 'R1',
      desc: `13-bit LSB FRAC = ${plan.frac25 & 0x1fff} (FRAC25 = ${plan.frac25})`,
    },
    {
      name: 'R0 (Init)',
      desc: `INT = ${plan.integerN}, 12-bit MSB FRAC = ${plan.frac25 >>> 13}, MUX = ${
        isRamp ? 'Ramp Complete (15)' : 'Digital Lock (6)'
      }, RAMP_ON = 0`,
    },
  ];

  return plan.words.map((word, index) => ({
    index,
    registerName: meta[index].name,
    word,
    hex: formatHex32(word),
    description: meta[index].desc,
  }));
}
