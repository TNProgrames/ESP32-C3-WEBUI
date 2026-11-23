// سائقات Lua الجاهزة (تُثبَّت في /scripts/lib وتُستعمل بـ require)
import ssd1306 from './drivers/ssd1306.lua?raw';
import bme280 from './drivers/bme280.lua?raw';

export const drivers = { ssd1306, bme280 };
