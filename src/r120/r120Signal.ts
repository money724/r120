// Port of User/r120_signal.c & User/r120_signal.h
// 4096-point in-place radix-2 DIT complex FFT with Hann window & log-power parabolic peak interpolation.

export const R120_SIG_MAX_SAMPLES = 2400;
export const R120_SIG_FFT_SIZE = 4096;
export const R120_SIG_MIN_BEAT_HZ = 500;
export const R120_SIG_MAX_BEAT_HZ = 180_000;

const R120_SIG_MIN_AMPLITUDE_COUNTS = 5.0;
const R120_SIG_MIN_PEAK_CONTRAST_DB = 18.0;

export enum R120SigStatus {
  Ok = 0,
  BadArgument = 1,
  NoClearPeak = 2,
}

export interface R120SigResult {
  sampleCount: number;
  sampleRateHz: number;
  adcMeanI: number;
  adcMeanQ: number;
  signedBeatHz: number;
  absBeatHz: number;
  amplitudeCounts: number;
  peakContrastDb: number;
  peakBin: number;
}

export interface R120SigAnalysis {
  status: R120SigStatus;
  result: R120SigResult;
  // FFT-shifted magnitude spectrum in dB relative to full-scale window sum (length 4096, index 0 = -fs/2 .. 4095 = +fs/2 - df)
  shiftedPowerDb: Float32Array;
  shiftedFreqHz: Float32Array;
  backgroundMeanDb: number;
}

export function clamp12(value: number): number {
  if (value <= 0) return 0;
  if (value >= 4095) return 4095;
  return Math.floor(value + 0.5);
}

function fft4096InPlace(re: Float64Array, im: Float64Array): void {
  const N = R120_SIG_FFT_SIZE;

  // Bit reversal
  for (let i = 1, j = 0; i < N; ++i) {
    let bit = N >>> 1;
    while ((j & bit) !== 0) {
      j ^= bit;
      bit >>>= 1;
    }
    j ^= bit;
    if (i < j) {
      const tr = re[i];
      const ti = im[i];
      re[i] = re[j];
      im[i] = im[j];
      re[j] = tr;
      im[j] = ti;
    }
  }

  for (let len = 2; len <= N; len <<= 1) {
    const half = len >>> 1;
    const angle = (-2.0 * Math.PI) / len;
    const stepRe = Math.cos(angle);
    const stepIm = Math.sin(angle);

    for (let base = 0; base < N; base += len) {
      let wr = 1.0;
      let wi = 0.0;
      for (let k = 0; k < half; ++k) {
        const ia = base + k;
        const ib = ia + half;
        const br = re[ib];
        const bi = im[ib];
        const tr = wr * br - wi * bi;
        const ti = wr * bi + wi * br;
        const ar = re[ia];
        const ai = im[ia];

        re[ia] = ar + tr;
        im[ia] = ai + ti;
        re[ib] = ar - tr;
        im[ib] = ai - ti;

        const nextWr = wr * stepRe - wi * stepIm;
        const nextWi = wr * stepIm + wi * stepRe;
        wr = nextWr;
        wi = nextWi;
      }
    }
  }
}

export function analyzePackedSignal(
  packed: Uint32Array,
  sampleCount: number,
  sampleRateHz: number
): R120SigAnalysis {
  const emptyResult: R120SigResult = {
    sampleCount: 0,
    sampleRateHz: 0,
    adcMeanI: 0,
    adcMeanQ: 0,
    signedBeatHz: 0,
    absBeatHz: 0,
    amplitudeCounts: 0,
    peakContrastDb: 0,
    peakBin: 0,
  };
  const shiftedPowerDb = new Float32Array(R120_SIG_FFT_SIZE);
  const shiftedFreqHz = new Float32Array(R120_SIG_FFT_SIZE);

  if (
    !packed ||
    sampleCount < 32 ||
    sampleCount > R120_SIG_MAX_SAMPLES ||
    sampleCount > packed.length ||
    sampleRateHz < 1000 ||
    sampleRateHz > 1_000_000
  ) {
    return {
      status: R120SigStatus.BadArgument,
      result: emptyResult,
      shiftedPowerDb,
      shiftedFreqHz,
      backgroundMeanDb: -120,
    };
  }

  const out: R120SigResult = { ...emptyResult, sampleCount, sampleRateHz };

  // Mean removal (ADC bias around ~2048)
  let meanI = 0;
  let meanQ = 0;
  for (let n = 0; n < sampleCount; ++n) {
    const v = packed[n];
    meanI += v & 0x0fff;
    meanQ += (v >>> 16) & 0x0fff;
  }
  meanI /= sampleCount;
  meanQ /= sampleCount;
  out.adcMeanI = meanI;
  out.adcMeanQ = meanQ;

  const fftRe = new Float64Array(R120_SIG_FFT_SIZE);
  const fftIm = new Float64Array(R120_SIG_FFT_SIZE);
  let windowSum = 0;

  for (let n = 0; n < sampleCount; ++n) {
    const phase = (2.0 * Math.PI * n) / (sampleCount - 1);
    const w = 0.5 * (1.0 - Math.cos(phase));
    const v = packed[n];
    const i = (v & 0x0fff) - meanI;
    const q = ((v >>> 16) & 0x0fff) - meanQ;
    fftRe[n] = i * w;
    fftIm[n] = q * w;
    windowSum += w;
  }

  fft4096InPlace(fftRe, fftIm);

  const powerAt = (idx: number): number => fftRe[idx] * fftRe[idx] + fftIm[idx] * fftIm[idx];

  // Build shifted spectrum for visualization (-fs/2 .. +fs/2)
  const halfN = R120_SIG_FFT_SIZE / 2;
  const normSq = windowSum > 0 ? windowSum * windowSum : 1;
  const binHz = sampleRateHz / R120_SIG_FFT_SIZE;
  for (let s = 0; s < R120_SIG_FFT_SIZE; ++s) {
    const k = (s + halfN) & (R120_SIG_FFT_SIZE - 1);
    const signedBin = s - halfN;
    shiftedFreqHz[s] = signedBin * binHz;
    const pNorm = Math.max(powerAt(k) / normSq, 1e-16);
    shiftedPowerDb[s] = 10 * Math.log10(pNorm);
  }

  let minBin = Math.floor(
    (R120_SIG_MIN_BEAT_HZ * R120_SIG_FFT_SIZE + sampleRateHz - 1) / sampleRateHz
  );
  let maxBin = Math.floor((R120_SIG_MAX_BEAT_HZ * R120_SIG_FFT_SIZE) / sampleRateHz);
  if (minBin < 3) minBin = 3;
  if (maxBin > halfN - 3) maxBin = halfN - 3;
  if (maxBin <= minBin) {
    return {
      status: R120SigStatus.BadArgument,
      result: out,
      shiftedPowerDb,
      shiftedFreqHz,
      backgroundMeanDb: -120,
    };
  }

  let validCount = 0;
  let peakPower = 0;
  let peakIndex = 0;
  for (let k = 0; k < R120_SIG_FFT_SIZE; ++k) {
    const absk = k <= halfN ? k : R120_SIG_FFT_SIZE - k;
    if (absk < minBin || absk > maxBin) continue;
    const p = powerAt(k);
    ++validCount;
    if (p > peakPower) {
      peakPower = p;
      peakIndex = k;
    }
  }

  if (validCount < 8 || peakPower <= 0 || windowSum <= 0) {
    return {
      status: R120SigStatus.NoClearPeak,
      result: out,
      shiftedPowerDb,
      shiftedFreqHz,
      backgroundMeanDb: -120,
    };
  }

  let otherPowerSum = 0;
  let otherCount = 0;
  for (let k = 0; k < R120_SIG_FFT_SIZE; ++k) {
    const absk = k <= halfN ? k : R120_SIG_FFT_SIZE - k;
    let dist = k >= peakIndex ? k - peakIndex : peakIndex - k;
    if (dist > halfN) dist = R120_SIG_FFT_SIZE - dist;
    if (absk < minBin || absk > maxBin || dist <= 3) continue;
    otherPowerSum += powerAt(k);
    ++otherCount;
  }

  if (otherCount === 0) {
    return {
      status: R120SigStatus.NoClearPeak,
      result: out,
      shiftedPowerDb,
      shiftedFreqHz,
      backgroundMeanDb: -120,
    };
  }

  if (otherPowerSum < 1e-12) otherPowerSum = 1e-12;
  const meanBgPower = otherPowerSum / otherCount;
  const contrastDb = 10 * Math.log10(peakPower / meanBgPower);
  const amp = Math.sqrt(peakPower) / windowSum;
  const backgroundMeanDb = 10 * Math.log10(Math.max(meanBgPower / normSq, 1e-16));

  out.peakContrastDb = contrastDb;
  out.amplitudeCounts = amp;
  out.peakBin = peakIndex;

  if (amp < R120_SIG_MIN_AMPLITUDE_COUNTS || contrastDb < R120_SIG_MIN_PEAK_CONTRAST_DB) {
    return {
      status: R120SigStatus.NoClearPeak,
      result: out,
      shiftedPowerDb,
      shiftedFreqHz,
      backgroundMeanDb,
    };
  }

  // Three-bin log-power parabolic interpolation
  const left = (peakIndex + R120_SIG_FFT_SIZE - 1) % R120_SIG_FFT_SIZE;
  const right = (peakIndex + 1) % R120_SIG_FFT_SIZE;
  const floorP = 1e-20;
  const l = Math.log(Math.max(powerAt(left), floorP));
  const c = Math.log(Math.max(peakPower, floorP));
  const r = Math.log(Math.max(powerAt(right), floorP));
  const denom = l - 2 * c + r;
  let interp = 0;
  if (Math.abs(denom) > 1e-6) {
    interp = (0.5 * (l - r)) / denom;
    if (interp > 0.5) interp = 0.5;
    if (interp < -0.5) interp = -0.5;
  }

  const signedBin = peakIndex <= halfN ? peakIndex : peakIndex - R120_SIG_FFT_SIZE;
  out.signedBeatHz = ((signedBin + interp) * sampleRateHz) / R120_SIG_FFT_SIZE;
  out.absBeatHz = Math.abs(out.signedBeatHz);

  return {
    status: R120SigStatus.Ok,
    result: out,
    shiftedPowerDb,
    shiftedFreqHz,
    backgroundMeanDb,
  };
}

export function runSignalSelfTest(): R120SigAnalysis {
  const sampleRate = 400_000;
  const signalHz = 27777.7778; // 5 m ideal beat at 5 GHz / 6 ms
  const amplitude = 680.0;
  const testSamples = new Uint32Array(R120_SIG_MAX_SAMPLES);

  for (let n = 0; n < R120_SIG_MAX_SAMPLES; ++n) {
    const phase = (2.0 * Math.PI * signalHz * n) / sampleRate;
    const i = clamp12(2048.0 + amplitude * Math.cos(phase));
    const q = clamp12(2030.0 + amplitude * Math.sin(phase));
    testSamples[n] = (i | (q << 16)) >>> 0;
  }

  return analyzePackedSignal(testSamples, R120_SIG_MAX_SAMPLES, sampleRate);
}
