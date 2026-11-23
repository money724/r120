import React, { useState } from 'react';
import {
  R120_POWER_STAGES,
  buildRs485ModbusFrame,
  decodeTmp125Raw,
  encodeTmp125Raw,
  formatHex16,
} from '../r120/r120Hardware';

interface PeripheralsViewProps {
  powerStage: number;
  onAdvancePower: () => void;
  onSafeOffPower: () => void;
  temperatureC: number;
  onChangeTemperatureC: (val: number) => void;
  slantDistanceM: number;
  tankHeightM: number;
}

export const PeripheralsView: React.FC<PeripheralsViewProps> = ({
  powerStage,
  onAdvancePower,
  onSafeOffPower,
  temperatureC,
  onChangeTemperatureC,
  slantDistanceM,
  tankHeightM,
}) => {
  const [eeprom, setEeprom] = useState<number[]>(() => {
    const arr = new Array<number>(32).fill(0xff);
    arr[0x00] = 0x52; // 'R'
    arr[0x01] = 0x31; // '1'
    arr[0x02] = 0x32; // '2'
    arr[0x03] = 0x30; // '0'
    arr[0x10] = 0x66; // Matches main.c test byte at 0x10
    return arr;
  });
  const [selectedAddr, setSelectedAddr] = useState<number>(0x10);
  const [writeHex, setWriteHex] = useState<string>('66');
  const [slaveAddr, setSlaveAddr] = useState<number>(1);

  const rawTmp125 = encodeTmp125Raw(temperatureC);
  const decodedTmp125 = decodeTmp125Raw(rawTmp125);
  const tmp125Bits = rawTmp125.toString(2).padStart(16, '0');

  const liquidHeightMm = Math.max(0, Math.round((tankHeightM - slantDistanceM) * 1000));
  const rs485Frame = buildRs485ModbusFrame(
    slaveAddr,
    slantDistanceM * 1000,
    liquidHeightMm,
    decodedTmp125
  );

  const handleWriteEeprom = () => {
    const parsed = parseInt(writeHex, 16);
    if (!Number.isFinite(parsed) || parsed < 0 || parsed > 255) return;
    setEeprom((prev) => {
      const next = [...prev];
      next[selectedAddr] = parsed;
      return next;
    });
  };

  return (
    <div className="grid grid-cols-1 xl:grid-cols-2 gap-6">
      {/* Left: R120 Power Rail Sequencer */}
      <div className="bg-white border border-slate-200 rounded-lg p-5 space-y-4">
        <div className="flex flex-wrap items-center justify-between gap-3">
          <div>
            <h2 className="text-base font-semibold text-slate-900">R120 Power-Up Sequencer</h2>
            <p className="text-xs text-slate-500 mt-0.5">
              5-stage stepped rail enable sequence ported from{' '}
              <code className="font-mono text-slate-700">r120_power.c</code>.
            </p>
          </div>
          <div className="flex items-center gap-2">
            <button
              type="button"
              onClick={onSafeOffPower}
              className="px-3 py-1.5 text-xs font-medium text-rose-700 bg-rose-50 border border-rose-200 rounded-md hover:bg-rose-100 whitespace-nowrap transition-colors"
            >
              Safe Off (Stage 0)
            </button>
            <button
              type="button"
              disabled={powerStage >= 5}
              onClick={onAdvancePower}
              className="px-3.5 py-1.5 text-xs font-medium text-white bg-blue-600 rounded-md hover:bg-blue-700 disabled:bg-slate-300 disabled:cursor-not-allowed whitespace-nowrap transition-colors"
            >
              Advance Stage ({powerStage}/5)
            </button>
          </div>
        </div>

        <div className="divide-y divide-slate-100 border border-slate-200 rounded-md overflow-hidden">
          {R120_POWER_STAGES.map((spec) => {
            const active = powerStage >= spec.stage;
            const isNext = powerStage + 1 === spec.stage;
            return (
              <div
                key={spec.stage}
                className={`px-3.5 py-2.5 flex items-center justify-between gap-3 ${
                  active ? 'bg-slate-50/70' : 'bg-white'
                }`}
              >
                <div className="min-w-0">
                  <div className="flex items-center gap-2 text-xs">
                    <span className="font-mono text-slate-400 tabular-nums">0{spec.stage}.</span>
                    <span className="font-mono font-semibold text-slate-800">{spec.pin}</span>
                    <span className="text-slate-400">·</span>
                    <span className="text-slate-700 font-medium truncate">{spec.railName}</span>
                  </div>
                  <p className="text-[11px] text-slate-500 mt-0.5">{spec.notes}</p>
                </div>
                <div className="text-right shrink-0">
                  <div className="text-xs font-mono tabular-nums text-slate-700">
                    {spec.expectedVoltage}
                  </div>
                  <div
                    className={`text-[11px] font-mono font-medium ${
                      active
                        ? 'text-emerald-700'
                        : isNext
                          ? 'text-blue-700'
                          : 'text-slate-400'
                    }`}
                  >
                    {active ? '● HIGH' : isNext ? '○ NEXT' : '○ LOW'}
                  </div>
                </div>
              </div>
            );
          })}
        </div>
      </div>

      {/* Right: TMP125 + 24LC32A EEPROM + RS485 */}
      <div className="space-y-6">
        {/* TMP125 SPI Sensor */}
        <div className="bg-white border border-slate-200 rounded-lg p-5 space-y-4">
          <div className="flex items-center justify-between">
            <div>
              <h2 className="text-base font-semibold text-slate-900">TMP125 SPI Temperature Probe</h2>
              <p className="text-xs text-slate-500 mt-0.5">
                10-bit two&apos;s-complement word on bits D14..D5 (0.25 °C/LSB) from{' '}
                <code className="font-mono text-slate-700">tmp125.c</code>.
              </p>
            </div>
            <div className="text-right font-mono tabular-nums">
              <div className="text-2xl font-semibold text-slate-900">
                {decodedTmp125.toFixed(2)}
                <span className="text-xs font-normal text-slate-400 ml-1">°C</span>
              </div>
              <div className="text-xs text-blue-700">RAW {formatHex16(rawTmp125)}</div>
            </div>
          </div>

          <div>
            <input
              type="range"
              min={-40}
              max={125}
              step={0.25}
              value={temperatureC}
              onChange={(e) => onChangeTemperatureC(Number(e.target.value))}
              className="w-full accent-blue-600"
              aria-label="TMP125 simulated board temperature"
            />
            <div className="flex justify-between text-[11px] font-mono text-slate-400 mt-1">
              <span>-40.00 °C</span>
              <span>25.00 °C</span>
              <span>125.00 °C</span>
            </div>
          </div>

          <div className="bg-slate-50 border border-slate-200 rounded p-2.5 flex flex-wrap items-center justify-between gap-2 text-xs font-mono">
            <span className="text-slate-500">16-bit SPI Shift Register</span>
            <span className="tabular-nums tracking-wider">
              <span className="text-slate-400">{tmp125Bits.slice(0, 1)}</span>
              <span className="text-blue-700 font-semibold mx-1">{tmp125Bits.slice(1, 11)}</span>
              <span className="text-slate-400">{tmp125Bits.slice(11)}</span>
            </span>
          </div>
        </div>

        {/* 24LC32A I2C EEPROM & RS485 Frame */}
        <div className="bg-white border border-slate-200 rounded-lg p-5 space-y-4">
          <div className="flex flex-wrap items-center justify-between gap-2">
            <div>
              <h2 className="text-base font-semibold text-slate-900">24LC32A EEPROM &amp; RS485 Frame</h2>
              <p className="text-xs text-slate-500 mt-0.5">
                I2C byte memory (<code className="font-mono text-slate-700">0x10</code> tested in{' '}
                <code className="font-mono text-slate-700">main.c</code>) and 115200 bps RS485 output.
              </p>
            </div>
            <div className="flex items-center gap-2 text-xs font-mono">
              <span className="text-slate-500">Addr 0x{selectedAddr.toString(16).toUpperCase().padStart(2, '0')}</span>
              <input
                type="text"
                maxLength={2}
                value={writeHex}
                onChange={(e) => setWriteHex(e.target.value.replace(/[^0-9a-fA-F]/g, '').toUpperCase())}
                className="w-12 px-2 py-1 border border-slate-300 rounded font-mono text-center text-slate-900"
                aria-label="EEPROM hex byte value"
              />
              <button
                type="button"
                onClick={handleWriteEeprom}
                className="px-3 py-1 bg-slate-900 text-white rounded hover:bg-slate-800 whitespace-nowrap"
              >
                Write Byte
              </button>
            </div>
          </div>

          <div className="grid grid-cols-8 gap-1 font-mono text-xs tabular-nums">
            {eeprom.map((val, idx) => (
              <button
                key={idx}
                type="button"
                onClick={() => {
                  setSelectedAddr(idx);
                  setWriteHex(val.toString(16).toUpperCase().padStart(2, '0'));
                }}
                className={`py-1.5 rounded border text-center transition-colors ${
                  selectedAddr === idx
                    ? 'border-blue-600 bg-blue-50 text-blue-800 font-semibold'
                    : idx === 0x10
                      ? 'border-slate-300 bg-slate-100 text-slate-900'
                      : 'border-slate-200 bg-white text-slate-600 hover:bg-slate-50'
                }`}
              >
                {val.toString(16).toUpperCase().padStart(2, '0')}
              </button>
            ))}
          </div>

          <div className="pt-3 border-t border-slate-100 space-y-1.5">
            <div className="flex items-center justify-between text-xs">
              <span className="text-slate-600 font-medium">RS485 Telemetry Frame (115200 8N1)</span>
              <label className="flex items-center gap-1.5 text-slate-500 font-mono">
                Slave ID
                <input
                  type="number"
                  min={1}
                  max={247}
                  value={slaveAddr}
                  onChange={(e) => setSlaveAddr(Math.max(1, Math.min(247, Number(e.target.value) || 1)))}
                  className="w-14 px-1.5 py-0.5 border border-slate-300 rounded text-slate-900 text-right"
                />
              </label>
            </div>
            <div className="bg-slate-50 border border-slate-200 rounded p-2.5 font-mono text-xs text-slate-800 tracking-wider overflow-x-auto whitespace-nowrap">
              {rs485Frame}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};
