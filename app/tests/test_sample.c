#include "minitest.h"
#include "sample.h"
#include <stdlib.h>

static Sample S;   /* too big for the stack */

int main(void) {
    /* 1 s silence, 0.5 s tone, 1 s silence */
    int n = SR * 5 / 2;
    float *d = calloc(n, sizeof(float));
    for (int i = SR; i < SR * 3 / 2; i++) d[i] = 0.5f * sinf(i / 10.0f);
    int start, end;
    sample_trim_range(d, n, &start, &end);
    CHECK(abs(start - (SR - SR * 5 / 1000)) <= 2, "trim start is 5 ms before the tone");
    CHECK(abs(end - (SR * 3 / 2 + SR * 60 / 1000)) <= 12, "trim end is 60 ms after the tone");

    float *quiet = calloc(SR, sizeof(float));
    for (int i = 0; i < SR; i++) quiet[i] = 0.005f;
    sample_trim_range(quiet, SR, &start, &end);
    CHECK(start == 0 && end == SR, "all-quiet recording is kept whole");

    /* Loop crossfade: a sine that does not end on a period boundary would click on a hard loop. */
    int m = SR / 2 + 137;
    float *sine = malloc(m * sizeof(float));
    for (int i = 0; i < m; i++) sine[i] = 0.8f * sinf(6.2831853f * 220.0f * i / SR);
    sample_set(&S, sine, m, 0);
    int fade = SR * 50 / 1000;
    CHECK(S.loopStart == fade, "50 ms fade -> loopStart");
    CHECK(S.length == m, "length unchanged");
    CHECK(S.data[m - fade - 1] == sine[m - fade - 1], "audio before the fade is untouched");
    double seamJump = fabs(S.data[m - 1] - S.data[S.loopStart]);
    double step = fabs(sine[1] - sine[0]);
    CHECK(seamJump < step * 3, "loop seam is continuous");
    CHECK(S.data[0] == 0.0f && fabs(S.data[100]) < fabs(sine[100]) + 1e-6, "start fade applied");

    sample_from_recording(&S, d, n);
    CHECK(S.trimmedMs >= 990 && S.trimmedMs <= 1000, "recording trimmed about 1000 ms");
    CHECK(!S.isBuiltIn, "recording is not built-in");

    sample_make_builtin(&S);
    CHECK(S.isBuiltIn && S.length == SR, "built-in tone is one second");
    float peak = 0; for (int i = 0; i < S.length; i++) if (fabsf(S.data[i]) > peak) peak = fabsf(S.data[i]);
    CHECK(peak > 0.3f && peak <= 1.0f, "built-in tone has sane level");
    free(d); free(quiet); free(sine);
    MT_END();
}
