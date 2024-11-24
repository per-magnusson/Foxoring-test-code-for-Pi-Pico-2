// Find the best rational approximation to a number between 0 and 1.
//
// target - a number between 0 and 1 (inclusive)
// maxdenom - the maximum allowed denominator
//
// The algorithm is based on Farey sequences/fractions. See
// https://web.archive.org/web/20181119092100/https://nrich.maths.org/6596
// a, b, c, d notation from
// https://en.wikipedia.org/wiki/Farey_sequence is used here (not
// from the above reference). I.e. narrow the interval between a/b
// and c/d by splitting it using the mediant (a+c)/(b+d) until we are 
// close enough with either endpoint, or we have a denominator that is
// bigger than what is allowed.
// Start with the interval 0 to 1 (i.e. 0/1 to 1/1).
// A simple implementation of just calculating the mediant (a+c)/(b+d) and
// iterating with the mediant replacing the worst value of a/b and c/d is very
// inefficient in cases where the target is close to a rational number
// with a small denominator, like e.g. when approximating 10^-6.
// The straightforward algorithm would need about 10^6 iterations as it 
// would try all of 1/1, 1/2, 1/3, 1/4, 1/5 etc. To resolve this slow
// convergence, at each step, it is calculated how many times the 
// interval will need to be narrowed from the same side and all those 
// steps are taken at once.
//
// More details about this can be found here:
// https://axotron.se/blog/fast-algorithm-for-rational-approximation-of-floating-point-numbers/
//
// Per Magnusson, SA5BYZ, 2024
// MIT license
//

#include "farey.h"
#include <arduino.h>


rational_t rational_approximation(double target, uint32_t maxdenom)
{
  rational_t retval;
  double mediant;  // float does not have enough resolution 
                      // to deal with single-digit differences 
                      // between numbers above 10^8.
  double N, Ndenom, Ndenom_min;
  double epsilon = 1e-10; // To handle rounding issues in conjunction with floor
  uint32_t a = 0, b = 1, c = 1, d = 1, ac, bd, Nint;
  const int maxIter = 100;

  if(target > 1) {
    // Invalid
    retval.numerator = 1;
    retval.denominator = 1;
    return retval;
  }
  if(target < 0) {
    // Invalid
    retval.numerator = 0;
    retval.denominator = 1;
    return retval;
  }
  if(maxdenom < 1) {
    maxdenom = 1;
  }

  mediant = 0;
  Ndenom_min = 1/((double) 10*maxdenom);
  int ii = 0;
  // Farey approximation loop
  while(1) {
    ac = a+c;
    bd = b+d;
    if(bd > maxdenom || ii > maxIter) {
      // The denominator has become too big, or too many iterations.  
    	// Select the best of a/b and c/d.
      if(target - a/(double)b < c/(double)d - target) {
        ac = a;
        bd = b;
      } else {
        ac = c;
        bd = d;
      }
      break;
    }
    mediant = ac/(double)bd;
    if(target < mediant) {
      // Discard c/d as the mediant is closer to the target.
      // How many times in a row should we do that?
      // N = (c - target*d)/(target*b - a), but need to check for division by zero
      Ndenom = target * (double)b - (double)a;
      if(Ndenom < Ndenom_min) {
        // Division by zero, or close to it!
        // This means that a/b is a very good approximation
        // as we would need to update the c/d side a 
        // very large number of times to get closer.
        // Use a/b and exit the loop.
        ac = a;
        bd = b;
        break;
      }
      N = (c - target * (double)d)/Ndenom;
      Nint = floor(N + epsilon);
      // Check if the denominator will become too large
      if(d + Nint*b > maxdenom) {
        // Limit N, as the denominator would otherwise become too large
        N = (maxdenom - d)/(double)b;
        Nint = floor(N);
      }
      // Fast forward to a good c/d.
      c = c + Nint*a;
      d = d + Nint*b;

    } else {

      // Discard a/b as the mediant is closer to the target.
      // How many times in a row should we do that?
      // N = (target*b - a)/(c - target*d), but need to check for division by zero
      Ndenom = (double)c - target * (double)d;
      if(Ndenom < Ndenom_min) {
        // Division by zero, or close to it!
        // This means that c/d is a very good approximation 
        // as we would need to update the a/b side a 
        // very large number of times to get closer.
        // Use c/d and exit the loop.
        ac = c;
        bd = d;
        break;
      }
      N = (target * (double)b - a)/Ndenom;
      Nint = floor(N + epsilon);
      // Check if the denominator will become too large
      if(b + Nint*d > maxdenom) {
        // Limit N, as the denominator would otherwise become too large
        N = (maxdenom - b)/(double)d;
        Nint = floor(N);
      }
      // Fast forward to a good a/b.
      a = a + Nint*c;
      b = b + Nint*d;
    }
    ii++;
  }

  retval.numerator = ac;
  retval.denominator = bd;
  return retval;
}


typedef struct {
  double target;
  uint32_t maxdenom;
  uint32_t expected_numerator;
  uint32_t expected_denominator;
} rational_test_case_t;


void test_rational_approx()
{
  rational_t result;

  rational_test_case_t test[] = { 
    {0, 3000, 0, 1},
    {1, 3000, 1, 1},
    {0.5, 3000, 1, 2},
    {0.5+1/3001.0, 3000, 751, 1501},
    {1/3001.0, 2500, 1, 2500},
    {1/3001.0, 1500, 0, 1},
    {1/3001.0, 3001, 1, 3001},
    {0.472757439, 1816, 564, 1193},
    {0.472757439, 1817, 859, 1817},
    {0.288, 100000000, 36, 125},
  };
  uint32_t n_tests = sizeof(test)/sizeof(test[0]);

  for(uint32_t ii = 0; ii < n_tests; ii++) {
    result = rational_approximation(test[ii].target, test[ii].maxdenom);
    Serial.print("target = ");
    Serial.print(test[ii].target, 7);
    Serial.print(", maxdenom = ");
    Serial.print(test[ii].maxdenom);
    Serial.print(", approx = ");
    Serial.print(result.numerator);
    Serial.print("/");
    Serial.print(result.denominator);
    if(result.numerator == test[ii].expected_numerator && result.denominator == test[ii].expected_denominator) {
      Serial.println(" OK");
    } else {
      Serial.print(" Expected: ");
      Serial.print(test[ii].expected_numerator);
      Serial.print("/");
      Serial.println(test[ii].expected_denominator);
    }
  }
}
