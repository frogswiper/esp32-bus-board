#!/bin/sh
# Regenerate the LVGL fonts (run inside node:22-alpine, repo mounted at /w).
set -e
npm i -g lv_font_conv@1.5.3 >/dev/null 2>&1
R="--range 0x20-0x7E --range 0xA0-0xFF --range 0x2013,0x2014,0x2022,0x2026,0x2192,0x2212"
gen() { # name ttf size fallback
  lv_font_conv --font /w/tools/fonts/$2 --size $3 $R --format lvgl --bpp 4 --no-compress \
    --lv-include lvgl.h --lv-font-name $1 --lv-fallback $4 -o /w/firmware/src/fonts/$1.c --force-fast-kern-format
}
mkdir -p /w/firmware/src/fonts
trap "chown -R 1000:1000 /w/firmware/src/fonts" EXIT
gen font_m12 Montserrat-Medium.ttf 12 lv_font_montserrat_12
gen font_m14 Montserrat-Medium.ttf 14 lv_font_montserrat_14
gen font_m16 Montserrat-Medium.ttf 16 lv_font_montserrat_16
gen font_m20 Montserrat-Medium.ttf 20 lv_font_montserrat_20
gen font_b16 Montserrat-Bold.ttf 16 lv_font_montserrat_16
gen font_b20 Montserrat-Bold.ttf 20 lv_font_montserrat_20
gen font_b24 Montserrat-Bold.ttf 24 lv_font_montserrat_24
gen font_b36 Montserrat-Bold.ttf 36 lv_font_montserrat_36
