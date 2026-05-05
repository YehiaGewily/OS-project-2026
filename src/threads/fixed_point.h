/* fixed_point.h
   17.14 Fixed-Point Arithmetic Macros for Pintos MLFQS */

#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>
typedef int fixed_t;
/* Binary point is at bit 14  →  F = 2^14 = 16384 */
#define F                   (1 << 14)

/* ---- Conversions --------------------------------------------------- */

/* Convert integer n to fixed-point */
#define INT_TO_FP(n)        ((n) * (F))

/* Convert fixed-point x to integer (truncate toward zero) */
#define FP_TO_INT(x)        ((x) / (F))

/* Convert fixed-point x to integer (round to nearest) */
#define FP_TO_INT_ROUND(x)  ((x) >= 0 ? ((x) + (F) / 2) / (F) \
                                      : ((x) - (F) / 2) / (F))

/* ---- Addition / Subtraction ---------------------------------------- */

/* fixed + fixed */
#define ADD_FP(x, y)        ((x) + (y))

/* fixed + integer */
#define ADD_MIXED(x, n)     ((x) + (n) * (F))

/* fixed - fixed */
#define SUB_FP(x, y)        ((x) - (y))

/* fixed - integer */
#define SUB_MIXED(x, n)     ((x) - (n) * (F))

/* ---- Multiplication ------------------------------------------------ */

/* fixed * fixed  (must use int64_t to prevent overflow!) */
#define MUL_FP(x, y)        ((int)((int64_t)(x) * (y) / (F)))

/* fixed * integer */
#define MUL_MIXED(x, n)     ((x) * (n))

/* ---- Division ------------------------------------------------------ */

/* fixed / fixed  (must use int64_t to prevent overflow!) */
#define DIV_FP(x, y)        ((int)((int64_t)(x) * (F) / (y)))

/* fixed / integer */
#define DIV_MIXED(x, n)     ((x) / (n))

#endif /* THREADS_FIXED_POINT_H */