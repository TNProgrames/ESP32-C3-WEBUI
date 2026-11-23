-- ssd1306: شاشة OLED عبر I2C (SSD1306 / SH1106). الاستخدام:
--   local oled = require("ssd1306").new({w=128, h=64})   -- addr=0x3C
--   oled:text(0,0,"Hello"); oled:show()
local M = {}
local O = {}
O.__index = O

local function cmd(self, ...)
  return i2c.write_reg(self.addr, 0x00, {...})
end

function M.new(o)
  o = o or {}
  local self = setmetatable({}, O)
  self.addr = o.addr or 0x3C
  self.w = o.w or 128
  self.h = o.h or 64
  self.sh1106 = o.sh1106 or false
  self.fb = fb.new(self.w, self.h)
  local com = (self.h == 64) and 0x12 or 0x02
  local seq = {
    0xAE, 0xD5, 0x80, 0xA8, self.h - 1, 0xD3, 0, 0x40, 0x8D, 0x14,
    0x20, 0x00, 0xA1, 0xC8, 0xDA, com, 0x81, 0xCF, 0xD9, 0xF1,
    0xDB, 0x40, 0xA4, 0xA6, 0xAF,
  }
  if self.sh1106 then seq[12] = 0x02 end
  local ok, err = cmd(self, table.unpack(seq))
  if not ok then return nil, err end
  return self
end

-- تمرير دوال الرسم إلى fb
for _, n in ipairs({"clear","invert","pixel","get","line","rect","circle","bar","text","text_width","size"}) do
  O[n] = function(self, ...) return self.fb[n](self.fb, ...) end
end

function O:show()
  local pages = self.h // 8
  if self.sh1106 then
    for p = 0, pages - 1 do
      cmd(self, 0xB0 + p, 0x02, 0x10)
      local d = self.fb:page(p)
      for i = 1, #d, 64 do
        local ok, err = i2c.write_reg(self.addr, 0x40, d:sub(i, i + 63))
        if not ok then return nil, err end
      end
    end
    return true
  end
  cmd(self, 0x21, 0, self.w - 1, 0x22, 0, pages - 1)
  local d = self.fb:data()
  for i = 1, #d, 64 do
    local ok, err = i2c.write_reg(self.addr, 0x40, d:sub(i, i + 63))
    if not ok then return nil, err end
  end
  return true
end

function O:contrast(v) return cmd(self, 0x81, v & 0xFF) end
function O:power(on) return cmd(self, on and 0xAF or 0xAE) end
function O:invert_display(on) return cmd(self, on and 0xA7 or 0xA6) end
function O:flip(on)
  cmd(self, on and 0xA0 or 0xA1)
  return cmd(self, on and 0xC0 or 0xC8)
end

return M
