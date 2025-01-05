#pragma once

#include <cstdint>
#include <cmath>

// Type to represent (positive) rational numbers
typedef struct {
  uint32_t numerator;
  uint32_t denominator;
  uint32_t iterations;   // Just for debugging of the Farey algorithm
} rational_t;


rational_t rational_approximation(double target, uint32_t maxdenom);
void test_rational_approx();
