#include <assert.h>
#include <stdio.h>
#include "dark_color.h"
int main(void) {
    for (unsigned v = 0; v <= 255; v++) {
        unsigned char gray[3] = {v, v, v};
        dark_color_filter(gray);
        unsigned expected = v <= 12 ? 0 : v >= 24 ? v : (v * (v - 12) + 6) / 12;
        for (unsigned c = 0; c < 3; c++) assert(gray[c] == expected);
    }
    unsigned char residual[3] = {4, 8, 2};
    dark_color_filter(residual);
    assert(!residual[0] && !residual[1] && !residual[2]);
    unsigned char transition[3] = {9, 18, 6};
    dark_color_filter(transition);
    assert(transition[0] == 5 && transition[1] == 9 && transition[2] == 3);
    unsigned char saturated[3] = {0, 24, 0};
    dark_color_filter(saturated);
    assert(saturated[0] == 0 && saturated[1] == 24 && saturated[2] == 0);
    puts("PASS: near-black suppression, gradual transition, unchanged brighter colors");
}
