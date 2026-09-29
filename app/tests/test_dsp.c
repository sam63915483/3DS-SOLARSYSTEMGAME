#include "minitest.h"
#include "dsp_util.h"

static float rms_of_sine(float hz, SVF *s, int which) {
    double acc = 0; int n = SR / 2;
    for (int i = 0; i < n; i++) {
        float x = sinf(TWO_PI * hz * i / SR), lp, hp, bp;
        svf_run(s, x, &lp, &hp, &bp);
        float y = which == 0 ? lp : (which == 1 ? hp : bp);
        if (i > n / 2) acc += y * y;
    }
    return sqrtf(acc / (n / 2));
}

int main(void) {
    SVF s; svf_set(&s, 1000.0f, 0.7f);
    float pass = rms_of_sine(100.0f, &s, 0);
    svf_set(&s, 1000.0f, 0.7f); s.lp = s.bp = 0;
    float stop = rms_of_sine(10000.0f, &s, 0);
    CHECK(pass > 0.6f, "SVF low-pass passes 100 Hz");
    CHECK(stop < pass * 0.1f, "SVF low-pass cuts 10 kHz by > 20 dB");
    svf_set(&s, 1000.0f, 0.7f); s.lp = s.bp = 0;
    float hpPass = rms_of_sine(8000.0f, &s, 1);
    svf_set(&s, 1000.0f, 0.7f); s.lp = s.bp = 0;
    float hpStop = rms_of_sine(50.0f, &s, 1);
    CHECK(hpPass > 0.5f && hpStop < hpPass * 0.1f, "SVF high-pass works");

    float storage[64]; DelayLine d; dl_init(&d, storage, 64);
    for (int i = 0; i < 64; i++) dl_write(&d, (float)i);
    CHECK_NEAR(dl_read(&d, 1.0f), 63.0f, 1e-5, "delay of 1 reads the last sample written");
    CHECK_NEAR(dl_read(&d, 10.0f), 54.0f, 1e-5, "delay of 10");
    CHECK_NEAR(dl_read(&d, 10.5f), 53.5f, 1e-5, "fractional delay interpolates");

    CHECK_NEAR(fast_tanh(0.0f), 0.0f, 1e-6, "tanh(0)");
    CHECK_NEAR(fast_tanh(1.0f), tanhf(1.0f), 0.02, "tanh(1) approx");
    CHECK(fast_tanh(10.0f) == 1.0f && fast_tanh(-10.0f) == -1.0f, "tanh clamps");
    CHECK_NEAR(midi_to_hz(69), 440.0f, 1e-3, "A4 = 440");
    CHECK_NEAR(midi_to_hz(60), 261.63f, 0.01, "C4 = 261.63");

    LFO l; lfo_set(&l, 1.0f); float mn = 9, mx = -9;
    for (int i = 0; i < SR; i++) { float v = lfo_run(&l); if (v < mn) mn = v; if (v > mx) mx = v; }
    CHECK(mn <= -0.99f && mx >= 0.99f, "LFO spans -1..1");
    MT_END();
}
