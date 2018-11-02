/*! \file fixed-point.c
 *
 * Implementation of the fixed-point arithmetic in the kernel of PintOS.
 */

#include "fixed-point.h"

/*! Converts a fixed-point number to an integer. */
int fixed_point_to_integer(fixed_point x) {
    return (int) (x / (1ull << NUM_FRAC_BITS));
}

/*! Converts an integer to a fixed-point number. */
fixed_point integer_to_fixed_point(int n) {
    return (fixed_point) (n * (1ull << NUM_FRAC_BITS));
}

/*! Adds an integer to a fixed-point number. */
fixed_point add(fixed_point x, int n) {
    return x + integer_to_fixed_point(n);
}

/*! Multiplies two fixed-point numbers together. */
fixed_point multiply(fixed_point x, fixed_point y) {
    return ((int64_t) x) * y / (1ull << NUM_FRAC_BITS);
}

/*! Divides a fixed-point number by a fixed-point number. */
fixed_point divide(fixed_point x, fixed_point y) {
    return ((int64_t) x) * (1ull << NUM_FRAC_BITS) / y;
}
