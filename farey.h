#pragma once

#include <cstdint>
#include <cmath>

typedef struct {
  uint32_t numerator;
  uint32_t denominator;
} rational_t;


rational_t rational_approximation(double target, uint32_t maxdenom);
void test_rational_approx();
