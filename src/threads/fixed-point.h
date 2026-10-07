#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* 17.14 Fixed-Point Representation
   p = 17, q = 14, f = 1 << 14 = 16384 */
typedef int fixed_point_t;

#define FP_SHIFT 14
#define FP_F (1 << FP_SHIFT)

/* Convert integer n to fixed-point */
static inline fixed_point_t fp_from_int(int n) {
  return n * FP_F;
}

/* Convert fixed-point x to integer, rounding toward zero */
static inline int fp_to_int_round_zero(fixed_point_t x) {
  return x / FP_F;
}

/* Convert fixed-point x to integer, rounding to nearest */
static inline int fp_to_int_round_nearest(fixed_point_t x) {
  if (x >= 0)
    return (x + FP_F / 2) / FP_F;
  else
    return (x - FP_F / 2) / FP_F;
}

/* Add two fixed-point numbers */
static inline fixed_point_t fp_add(fixed_point_t x, fixed_point_t y) {
  return x + y;
}

/* Subtract two fixed-point numbers (x - y) */
static inline fixed_point_t fp_sub(fixed_point_t x, fixed_point_t y) {
  return x - y;
}

/* Add fixed-point x and integer n */
static inline fixed_point_t fp_add_int(fixed_point_t x, int n) {
  return x + n * FP_F;
}

/* Subtract integer n from fixed-point x */
static inline fixed_point_t fp_sub_int(fixed_point_t x, int n) {
  return x - n * FP_F;
}

/* Multiply two fixed-point numbers */
static inline fixed_point_t fp_mul(fixed_point_t x, fixed_point_t y) {
  return ((int64_t) x) * y / FP_F;
}

/* Multiply fixed-point x by integer n */
static inline fixed_point_t fp_mul_int(fixed_point_t x, int n) {
  return x * n;
}

/* Divide two fixed-point numbers (x / y) */
static inline fixed_point_t fp_div(fixed_point_t x, fixed_point_t y) {
  return ((int64_t) x) * FP_F / y;
}

/* Divide fixed-point x by integer n */
static inline fixed_point_t fp_div_int(fixed_point_t x, int n) {
  return x / n;
}

#endif /* threads/fixed-point.h */
