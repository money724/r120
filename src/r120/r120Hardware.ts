// Software simulation of board peripherals from User/r120_power.c, User/tmp125.c,
// User/24lc32a.c, User/r120_acq.c, and User/oled.c.

export interface PowerStageSpec {
  stage: number;
  pin: string;
  railName: string;
  expectedVoltage: string;
  settlingMs: number;
  notes: string;
}

export const R120_POWER_STAGES: PowerStageSpec[] = [
  {
    stage: 1,
    pin: 'AN3V3_EN',
    railName: 'Analog 3.3V Rail',
    expectedVoltage: '3.30 V',
    settlingMs: 10,
    notes: 'Enables analog front-end and IF amplifier supply.',
  },
  {
    stage: 2,
    pin: 'ADF18_EN',
    railName: 'ADF4159 1.8V LDO',
    expectedVoltage: '1.80 V',
    settlingMs: 10,
    notes: 'Powers ADF4159 PLL core and digital interface.',
  },
  {
    stage: 3,
    pin: 'ADF_CLK_EN',
    railName: '75 MHz Reference Oscillator',
    expectedVoltage: '3.30 V / 75 MHz',
    settlingMs: 10,
    notes: 'ASDMB startup max ~3 ms per datasheet; 10 ms margin.',
  },
  {
    stage: 4,
    pin: 'TRX_REN',
    railName: 'TRX120 Divider & TX Power',
    expectedVoltage: '~1.20 V (R10/R11)',
    settlingMs: 1,
    notes: 'Enables /64 divider and sets nominal 120 GHz TX power.',
  },
  {
    stage: 5,
    pin: 'ADF_CE',
    railName: 'ADF4159 Chip Enable',
    expectedVoltage: 'HIGH (3.30 V)',
    settlingMs: 1,
    notes: 'Required before ADF4159_CFG_Apply() hardware SPI writes.',
  },
];

// TMP125 10-bit two's-complement encoder/decoder matching User/tmp125.c
// D14..D5 = 10-bit signed temperature, LSB = 0.25 C, D15 = 0, D4..D0 repeat D5.
export function encodeTmp125Raw(tempC: number): number {
  const clamped = Math.max(-55, Math.min(125, tempC));
  const steps = Math.round(clamped / 0.25); // -220 .. +500
  const temp10 = steps & 0x03ff;
  const d5 = temp10 & 0x01;
  const low5 = d5 ? 0x1f : 0x00;
  return ((temp10 << 5) | low5) & 0x7fff;
}

export function decodeTmp125Raw(raw16: number): number {
  let temp10 = (raw16 & 0xffff) >> 5;
  if (temp10 & 0x0200) {
    temp10 = temp10 - 1024;
  }
  return temp10 * 0.25;
}

export function formatHex16(word: number): string {
  return '0x' + (word & 0xffff).toString(16).toUpperCase().padStart(4, '0');
}

// RS485 frame formatter representing sensor level + temperature telemetry
export function buildRs485ModbusFrame(
  slaveAddr: number,
  distanceMm: number,
  liquidHeightMm: number,
  tempC: number
): string {
  const bytes: number[] = [
    slaveAddr & 0xff,
    0x03, // Read Holding Registers response
    0x06, // 6 data bytes (3x uint16)
    (Math.max(0, Math.round(distanceMm)) >> 8) & 0xff,
    Math.max(0, Math.round(distanceMm)) & 0xff,
    (Math.max(0, Math.round(liquidHeightMm)) >> 8) & 0xff,
    Math.max(0, Math.round(liquidHeightMm)) & 0xff,
    (Math.round(tempC * 10) >> 8) & 0xff,
    Math.round(tempC * 10) & 0xff,
  ];
  // Standard Modbus RTU CRC-16
  let crc = 0xffff;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; ++i) {
      if (crc & 1) crc = (crc >>> 1) ^ 0xa001;
      else crc >>>= 1;
    }
  }
  bytes.push(crc & 0xff, (crc >>> 8) & 0xff);
  return bytes.map((b) => b.toString(16).toUpperCase().padStart(2, '0')).join(' ');
}
