#ifndef BLACKBARS_H
#define BLACKBARS_H
/* Calibrated preset-0 edge probes on this G4. Return -1 for an ambiguous
 * or dark frame so darkness alone cannot switch the active capture mode. */
static inline int letterbox_candidate(const unsigned black[16], const unsigned char color[16][3]) {
    const unsigned interior[] = {2, 6, 10, 15};
    unsigned lit = 0;
    for (unsigned i = 0; i < 4; i++) {
        unsigned r = interior[i], peak = color[r][0];
        for (unsigned c = 1; c < 3; c++)
            if (color[r][c] > peak)
                peak = color[r][c];
        if (black[r] < 20 && peak > 35)
            lit++;
    }
    if (lit < 2)
        return -1;
    unsigned top = (black[3] + black[11] + black[12]) / 3;
    unsigned bottom = (black[1] + black[5] + black[9]) / 3;
    if (top >= 55 && bottom >= 25)
        return 1;
    if (top < 20 && bottom < 20)
        return 0;
    return -1;
}
#endif
