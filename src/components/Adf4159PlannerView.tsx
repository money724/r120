import React, { useMemo, useState } from 'react';
import {
  ADF4159_CFG_REF_HZ,
  ADF4159_CFG_TRX_DIVIDER,
  Adf4159CfgMode,
  Adf4159CfgPlan,
  describePlanRegisters,
  formatHex32,
  planFixedHz,
  planRampHz,
} from '../r120/adf4159Config';
import { ArrowRight } from 'lucide-react';

interface Adf4159PlannerViewProps {
  onApplyToPipeline: (bandwidthHz: number, durationUs: number, startGhz: number, stopGhz: number) => void;
  powerStage: number;
}

export const Adf4159PlannerView: React.FC<Adf4159PlannerViewProps> = ({
  onApplyToPipeline,
  powerStage,
}) => {
  const [mode, setMode] = useState<Adf4159CfgMode>(Adf4159CfgMode.SingleSawtooth);
  const [startGhz, setStartGhz] = useState<number>(120.0);
  const [stopGhz, setStopGhz] = useState<number>(125.0);
  const [durationUs, setDurationUs] = useState<number>(6000);
  const [fixedGhz, setFixedGhz] = useState<number>(122.5);
  const [appliedNotice, setAppliedNotice] = useState<string | null>(null);

  const outcome = useMemo(() => {
    if (mode === Adf4159CfgMode.Fixed) {
      const rfHz = BigInt(Math.round(fixedGhz * 1_000_000_000));
      return planFixedHz(rfHz);
    }
    const startHz = BigInt(Math.round(startGhz * 1_000_000_000));
    const stopHz = BigInt(Math.round(stopGhz * 1_000_000_000));
    return planRampHz(startHz, stopHz, durationUs);
  }, [mode, startGhz, stopGhz, durationUs, fixedGhz]);

  const plan: Adf4159CfgPlan | null = outcome.plan;
  const registers = useMemo(() => (plan ? describePlanRegisters(plan) : []), [plan]);

  const handleApply = () => {
    if (!plan || plan.mode !== Adf4159CfgMode.SingleSawtooth) return;
    onApplyToPipeline(
      Number(plan.actualBandwidthHz),
      plan.actualDurationUs,
      Number(plan.actualStartHz) / 1e9,
      Number(plan.actualStopHz) / 1e9
    );
    setAppliedNotice(
      `Applied ${(Number(plan.actualBandwidthHz) / 1e9).toFixed(4)} GHz / ${plan.actualDurationUs} µs sweep to Signal Pipeline.`
    );
  };

  return (
    <div className="grid grid-cols-1 lg:grid-cols-[360px_1fr] gap-6 items-start">
      {/* Left Configuration Column */}
      <div className="bg-white border border-slate-200 rounded-lg p-5 space-y-5">
        <div>
          <h2 className="text-base font-semibold text-slate-900">ADF4159 Synthesizer Planner</h2>
          <p className="text-xs text-slate-500 mt-1 leading-relaxed">
            Exact 64-bit integer register calculation ported from{' '}
            <code className="font-mono text-slate-700">adf4159_config.c</code>. REFIN ={' '}
            {Number(ADF4159_CFG_REF_HZ) / 1e6} MHz, TRX120 divider = /{Number(ADF4159_CFG_TRX_DIVIDER)},
            fractional modulus = 2²⁵.
          </p>
        </div>

        {/* Mode Toggle */}
        <div>
          <label className="block text-xs font-medium text-slate-600 mb-1.5">Operating Mode</label>
          <div className="grid grid-cols-2 gap-1 p-1 bg-slate-100 rounded-md">
            <button
              type="button"
              onClick={() => {
                setMode(Adf4159CfgMode.SingleSawtooth);
                setAppliedNotice(null);
              }}
              className={`py-1.5 px-3 text-xs font-medium rounded whitespace-nowrap transition-colors ${
                mode === Adf4159CfgMode.SingleSawtooth
                  ? 'bg-white text-slate-900 shadow-xs'
                  : 'text-slate-600 hover:text-slate-900'
              }`}
            >
              Single Sawtooth
            </button>
            <button
              type="button"
              onClick={() => {
                setMode(Adf4159CfgMode.Fixed);
                setAppliedNotice(null);
              }}
              className={`py-1.5 px-3 text-xs font-medium rounded whitespace-nowrap transition-colors ${
                mode === Adf4159CfgMode.Fixed
                  ? 'bg-white text-slate-900 shadow-xs'
                  : 'text-slate-600 hover:text-slate-900'
              }`}
            >
              Fixed CW
            </button>
          </div>
        </div>

        {mode === Adf4159CfgMode.SingleSawtooth ? (
          <div className="space-y-4">
            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-slate-600 font-medium">Start RF Frequency</span>
                <span className="font-mono tabular-nums text-slate-900">{startGhz.toFixed(3)} GHz</span>
              </div>
              <input
                type="range"
                min={120.0}
                max={124.8}
                step={0.05}
                value={startGhz}
                onChange={(e) => {
                  setStartGhz(Number(e.target.value));
                  setAppliedNotice(null);
                }}
                className="w-full accent-blue-600"
              />
            </div>

            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-slate-600 font-medium">Stop RF Frequency</span>
                <span className="font-mono tabular-nums text-slate-900">{stopGhz.toFixed(3)} GHz</span>
              </div>
              <input
                type="range"
                min={120.2}
                max={125.0}
                step={0.05}
                value={stopGhz}
                onChange={(e) => {
                  setStopGhz(Number(e.target.value));
                  setAppliedNotice(null);
                }}
                className="w-full accent-blue-600"
              />
            </div>

            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-slate-600 font-medium">Ramp Duration</span>
                <span className="font-mono tabular-nums text-slate-900">{durationUs} µs</span>
              </div>
              <input
                type="range"
                min={1000}
                max={50000}
                step={500}
                value={durationUs}
                onChange={(e) => {
                  setDurationUs(Number(e.target.value));
                  setAppliedNotice(null);
                }}
                className="w-full accent-blue-600"
              />
              <div className="flex justify-between text-[11px] font-mono text-slate-400 mt-1">
                <span>1,000 µs</span>
                <span>6,000 µs (nominal)</span>
                <span>50,000 µs</span>
              </div>
            </div>

            <div className="pt-2 border-t border-slate-100 flex flex-col gap-2">
              <button
                type="button"
                disabled={!plan}
                onClick={handleApply}
                className="w-full inline-flex items-center justify-center gap-2 px-4 py-2 text-xs font-medium text-white bg-blue-600 rounded-md hover:bg-blue-700 disabled:bg-slate-300 disabled:cursor-not-allowed transition-colors whitespace-nowrap"
              >
                Apply Sweep to Signal Pipeline
                <ArrowRight className="w-3.5 h-3.5" />
              </button>
              {appliedNotice && (
                <p className="text-xs text-emerald-700 font-medium">{appliedNotice}</p>
              )}
            </div>
          </div>
        ) : (
          <div className="space-y-4">
            <div>
              <div className="flex justify-between text-xs mb-1">
                <span className="text-slate-600 font-medium">Fixed CW Frequency</span>
                <span className="font-mono tabular-nums text-slate-900">{fixedGhz.toFixed(3)} GHz</span>
              </div>
              <input
                type="range"
                min={120.0}
                max={125.0}
                step={0.05}
                value={fixedGhz}
                onChange={(e) => setFixedGhz(Number(e.target.value))}
                className="w-full accent-blue-600"
              />
            </div>
          </div>
        )}

        {/* Hardware apply interlock indicator */}
        <div className="pt-3 border-t border-slate-100 text-xs text-slate-600 space-y-1">
          <div className="flex items-center justify-between">
            <span>Hardware Write Interlock</span>
            <span
              className={`font-mono font-medium ${
                powerStage === 5 ? 'text-emerald-700' : 'text-amber-700'
              }`}
            >
              {powerStage === 5 ? '● Stage 5 Ready' : `▲ Stage ${powerStage}/5`}
            </span>
          </div>
          <p className="text-[11px] text-slate-500">
            Firmware requires <code className="font-mono">r120_power_stage == 5</code> and{' '}
            <code className="font-mono">ADF_CE = HIGH</code> before SPI writes.
          </p>
        </div>
      </div>

      {/* Right Register Map & Quantization Summary */}
      <div className="space-y-5 min-w-0">
        {outcome.error || !plan ? (
          <div className="bg-white border border-rose-200 rounded-lg p-5">
            <h3 className="text-sm font-semibold text-rose-700">✖ Invalid Sweep Specification</h3>
            <p className="text-xs text-slate-600 mt-1">{outcome.error}</p>
          </div>
        ) : (
          <>
            {/* Quantized Ramp Metrics */}
            <div className="grid grid-cols-2 xl:grid-cols-4 gap-3">
              <div className="bg-white border border-slate-200 rounded-lg p-3.5">
                <div className="text-xs text-slate-500">Actual Start RF</div>
                <div className="text-lg font-mono font-semibold text-slate-900 tabular-nums mt-1">
                  {(Number(plan.actualStartHz) / 1e9).toFixed(6)}
                  <span className="text-xs font-normal text-slate-400 ml-1">GHz</span>
                </div>
                <div className="text-[11px] font-mono text-slate-500 tabular-nums mt-0.5">
                  INT {plan.integerN} · FRAC {plan.frac25}
                </div>
              </div>

              <div className="bg-white border border-slate-200 rounded-lg p-3.5">
                <div className="text-xs text-slate-500">Actual Bandwidth</div>
                <div className="text-lg font-mono font-semibold text-slate-900 tabular-nums mt-1">
                  {(Number(plan.actualBandwidthHz) / 1e9).toFixed(6)}
                  <span className="text-xs font-normal text-slate-400 ml-1">GHz</span>
                </div>
                <div className="text-[11px] font-mono text-slate-500 tabular-nums mt-0.5">
                  Err{' '}
                  {Number(plan.actualBandwidthHz - (plan.requestedStopHz - plan.requestedStartHz)) >= 0
                    ? '+'
                    : ''}
                  {Number(plan.actualBandwidthHz - (plan.requestedStopHz - plan.requestedStartHz))} Hz
                </div>
              </div>

              <div className="bg-white border border-slate-200 rounded-lg p-3.5">
                <div className="text-xs text-slate-500">Actual Duration</div>
                <div className="text-lg font-mono font-semibold text-slate-900 tabular-nums mt-1">
                  {plan.actualDurationUs}
                  <span className="text-xs font-normal text-slate-400 ml-1">µs</span>
                </div>
                <div className="text-[11px] font-mono text-slate-500 tabular-nums mt-0.5">
                  Steps {plan.steps} · CLK1={plan.clk1}
                </div>
              </div>

              <div className="bg-white border border-slate-200 rounded-lg p-3.5">
                <div className="text-xs text-slate-500">Deviation Word</div>
                <div className="text-lg font-mono font-semibold text-slate-900 tabular-nums mt-1">
                  {plan.deviation}
                  <span className="text-xs font-normal text-slate-400 ml-1">
                    × 2^{plan.deviationOffset}
                  </span>
                </div>
                <div className="text-[11px] font-mono text-slate-500 tabular-nums mt-0.5">
                  R0 ON {formatHex32(plan.r0RampOn)}
                </div>
              </div>
            </div>

            {/* 11-Word SPI Register Sequence Table */}
            <div className="bg-white border border-slate-200 rounded-lg overflow-hidden">
              <div className="flex items-center justify-between px-4 py-3 border-b border-slate-200 bg-slate-50/70">
                <h3 className="text-xs font-semibold text-slate-800">
                  11-Word SPI Initialization Sequence (R7 → R0)
                </h3>
                <span className="text-xs font-mono text-slate-500 tabular-nums">
                  Tag {formatHex32(plan.validTag)}
                </span>
              </div>
              <div className="overflow-x-auto">
                <table className="w-full text-left border-collapse text-xs">
                  <thead>
                    <tr className="border-b border-slate-200 bg-slate-50/40 text-slate-500">
                      <th className="py-2 px-4 font-medium w-12">#</th>
                      <th className="py-2 px-4 font-medium w-32">Register</th>
                      <th className="py-2 px-4 font-medium w-36">32-bit SPI Word</th>
                      <th className="py-2 px-4 font-medium">Decoded Bitfields</th>
                    </tr>
                  </thead>
                  <tbody className="divide-y divide-slate-100">
                    {registers.map((reg) => (
                      <tr key={reg.index} className="hover:bg-slate-50/70">
                        <td className="py-2 px-4 font-mono text-slate-400 tabular-nums">{reg.index}</td>
                        <td className="py-2 px-4 font-mono font-medium text-slate-800 whitespace-nowrap">
                          {reg.registerName}
                        </td>
                        <td className="py-2 px-4 font-mono font-semibold text-blue-700 tabular-nums whitespace-nowrap">
                          {reg.hex}
                        </td>
                        <td className="py-2 px-4 text-slate-600">{reg.description}</td>
                      </tr>
                    ))}
                  </tbody>
                </table>
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
};
