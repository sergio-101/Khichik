#pragma once
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#define internal static
#define local static
#define global static
#define PI 3.14159265358979323846
#define AC_LUMA_COUNT 162

static void write_word(FILE *f, uint16_t v){ fputc((v>>8)&0xFF, f); fputc(v&0xFF, f); }
static void write_marker(FILE *f, uint16_t mark){ fputc(0xFF, f); fputc(mark & 0xFF, f); }

void dump_arr(int8_t *arr, int len, int byte_per_element);

int bit_length(int value);

static inline double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
