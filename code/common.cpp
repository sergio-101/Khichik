#include "common.h"

void dump_arr(int8_t *arr, int len, int byte_per_element) {
    // for (int i = 0; i < len * byte_per_element; i++) {
    //     printf("%02X ", arr[i] & 0xff);
    //     if ((i+1) % 16 == 0) printf("\n");
    // }
    for (int i = 0; i < len * byte_per_element; i++) {
        printf("%d ",arr[i]);
        if ((i+1) % 16 == 0) printf("\n");
    }
}

int bit_length(int value) {
    if (value == 0) return 0;
    int count = 0;
    value = abs(value);
    while (value != 0) {
        value >>= 1;
        count++;
    }
    return count;
}

