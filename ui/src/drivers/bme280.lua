-- bme280: حساس حرارة/ضغط/رطوبة (BMP280 بلا رطوبة). الاستخدام:
--   local s = require("bme280").new()          -- addr=0x76
--   local r = s:read()  -- {temp=°C, press=Pa, hum=%}
local M = {}
local S = {}
S.__index = S

local function sar(x, n) return x // (1 << n) end

function M.new(o)
  o = o or {}
  local self = setmetatable({}, S)
  self.addr = o.addr or 0x76
  local id, err = i2c.read_reg(self.addr, 0xD0)
  if not id then return nil, err end
  if id ~= 0x60 and id ~= 0x58 then return nil, "bad_id" end
  self.hasHum = (id == 0x60)
  local b1 = i2c.read_reg(self.addr, 0x88, 26)
  if not b1 then return nil, "calib" end
  local c = {string.unpack("<I2i2i2I2i2i2i2i2i2i2i2i2", string.char(table.unpack(b1)))}
  self.T1, self.T2, self.T3 = c[1], c[2], c[3]
  self.P1, self.P2, self.P3, self.P4, self.P5, self.P6, self.P7, self.P8, self.P9 =
    c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11], c[12]
  if self.hasHum then
    self.H1 = b1[26]
    local b2 = i2c.read_reg(self.addr, 0xE1, 7)
    if not b2 then return nil, "calib" end
    self.H2 = string.unpack("<i2", string.char(b2[1], b2[2]))
    self.H3 = b2[3]
    local h4 = (b2[4] << 4) | (b2[5] & 0x0F)
    local h5 = (b2[6] << 4) | (b2[5] >> 4)
    if h4 > 2047 then h4 = h4 - 4096 end
    if h5 > 2047 then h5 = h5 - 4096 end
    self.H4, self.H5 = h4, h5
    self.H6 = string.unpack("<i1", string.char(b2[7]))
    i2c.write_reg(self.addr, 0xF2, 0x01)
  end
  i2c.write_reg(self.addr, 0xF5, 0xA0)
  i2c.write_reg(self.addr, 0xF4, 0x27)
  return self
end

function S:compensate(adcT, adcP, adcH)
  local v1 = sar((sar(adcT, 3) - (self.T1 << 1)) * self.T2, 11)
  local v2 = sar(sar((sar(adcT, 4) - self.T1) * (sar(adcT, 4) - self.T1), 12) * self.T3, 14)
  local tf = v1 + v2
  local temp = sar(tf * 5 + 128, 8) / 100

  local press
  local a = tf - 128000
  local b = a * a * self.P6
  b = b + ((a * self.P5) << 17)
  b = b + (self.P4 << 35)
  a = sar(a * a * self.P3, 8) + ((a * self.P2) << 12)
  a = sar((1 << 47) + a, 0) * self.P1
  a = sar(a, 33)
  if a ~= 0 then
    local p = 1048576 - adcP
    p = ((p << 31) - b) * 3125 // a
    local x1 = sar(self.P9 * sar(p, 13) * sar(p, 13), 25)
    local x2 = sar(self.P8 * p, 19)
    p = sar(p + x1 + x2, 8) + (self.P7 << 4)
    press = p / 256
  end

  local hum
  if self.hasHum and adcH then
    local x = tf - 76800
    local u = sar(((adcH << 14) - (self.H4 << 20) - (self.H5 * x)) + 16384, 15)
    local w = sar(x * self.H6, 10) * (sar(x * self.H3, 11) + 32768)
    w = (sar(w, 10) + 2097152) * self.H2 + 8192
    x = u * sar(w, 14)
    x = x - sar(sar(sar(x, 15) * sar(x, 15), 7) * self.H1, 4)
    if x < 0 then x = 0 end
    if x > 419430400 then x = 419430400 end
    hum = sar(x, 12) / 1024
  end
  return temp, press, hum
end

function S:read()
  local d, err = i2c.read_reg(self.addr, 0xF7, self.hasHum and 8 or 6)
  if not d then return nil, err end
  local adcP = (d[1] << 12) | (d[2] << 4) | (d[3] >> 4)
  local adcT = (d[4] << 12) | (d[5] << 4) | (d[6] >> 4)
  local adcH = self.hasHum and ((d[7] << 8) | d[8]) or nil
  local t, p, h = self:compensate(adcT, adcP, adcH)
  return {temp = t, press = p, hum = h}
end

return M
