#pragma once
// LVGL heap in PSRAM: objects/styles are small (< 4 KB) so plain malloc would put them all in the ~90 KB of
// internal RAM that TLS needs. Long trip views create hundreds of objects.
#include <stdlib.h>
#include <esp_heap_caps.h>
static inline void* lv_psram_malloc(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(n);
}
static inline void* lv_psram_realloc(void* p, size_t n) {
    void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return q ? q : realloc(p, n);
}
