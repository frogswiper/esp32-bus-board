#pragma once
#include "settings.h"
// Two-language UI strings: TR("norsk", "English"). Language changes take effect on the next redraw.
inline uint8_t i18n_lang() { return settings_get().lang; }
#define TR(no, en) (i18n_lang() == LANG_EN ? (en) : (no))
