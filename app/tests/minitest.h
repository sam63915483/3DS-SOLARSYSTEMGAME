#ifndef MINITEST_H
#define MINITEST_H
#include <stdio.h>
#include <math.h>
static int mt_fail = 0, mt_pass = 0;
#define CHECK(cond, msg) do { if (cond) { mt_pass++; printf("ok   %s\n", msg); } \
    else { mt_fail++; printf("FAIL %s  (%s:%d)\n", msg, __FILE__, __LINE__); } } while (0)
#define CHECK_NEAR(a, b, eps, msg) CHECK(fabs((double)(a) - (double)(b)) <= (eps), msg)
#define MT_END() do { printf("%d passed, %d failed\n", mt_pass, mt_fail); return mt_fail ? 1 : 0; } while (0)
#endif
