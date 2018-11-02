/*! \file fixed-point.h
 *
 * Declarations for the fixed-point arithmetic in the kernel of PintOS.
 */

#include <stdint.h>

/*! The number of fractional bits in the fixed-point number representation. */
#define NUM_FRAC_BITS 14

/*! The fixed-point type used to represent fixed-point numbers in the
    kernel. */
typedef int fixed_point;

/*! Conversion functions. */
int fixed_point_to_integer(fixed_point x);
fixed_point integer_to_fixed_point(int n);

/*! Arithmetic functions. */
fixed_point add(fixed_point x, int n);
fixed_point multiply(fixed_point x, fixed_point y);
fixed_point divide(fixed_point x, fixed_point y);
