#include "minitest.h"
#include "fight.h"
#include <string.h>

/* The random draws behind the fishing must be fair: no streaks of rares, no run of heavy fish. */
int main(void) {
    uint32_t s = 0x9E3779B9u;
    /* a long sequence of consecutive pairs (as tier then weight are drawn) is uncorrelated */
    int n = 20000, quad[4] = { 0, 0, 0, 0 };
    double sum = 0;
    for (int i = 0; i < n; i++) {
        float a = frandf(&s, 0, 1), b = frandf(&s, 0, 1);
        sum += a;
        quad[(a < 0.5f ? 0 : 1) + (b < 0.5f ? 0 : 2)]++;
    }
    CHECK(fabs(sum / n - 0.5) < 0.02, "draws average one half");
    for (int q = 0; q < 4; q++) CHECK(fabs(quad[q] / (double)n - 0.25) < 0.02, "consecutive pairs fill every quadrant evenly");
    /* nearby seeds do not roll alike */
    uint32_t s1 = 1, s2 = 2;
    int same = 0;
    for (int i = 0; i < 100; i++) if (fabsf(frandf(&s1, 0, 1) - frandf(&s2, 0, 1)) < 0.01f) same++;
    CHECK(same < 5, "seeds 1 and 2 give different sequences");
    MT_END();
}
