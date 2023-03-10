#ifndef _BACKPORT_LINUX_CORDIC_H
#define _BACKPORT_LINUX_CORDIC_H 1

#include <linux/version.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,1,0))
#include_next <linux/cordic.h>
#else

/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __CORDIC_H_
#define __CORDIC_H_

#include <linux/types.h>

/**
 * struct cordic_iq - i/q coordinate.
 *
 * @i: real part of coordinate (in phase).
 * @q: imaginary part of coordinate (quadrature).
 */
struct cordic_iq {
	s32 i;
	s32 q;
};

/**
 * cordic_calc_iq() - calculates the i/q coordinate for given angle.
 *
 * @theta: angle in degrees for which i/q coordinate is to be calculated.
 * @coord: function output parameter holding the i/q coordinate.
 *
 * The function calculates the i/q coordinate for a given angle using
 * cordic algorithm. The coordinate consists of a real (i) and an
 * imaginary (q) part. The real part is essentially the cosine of the
 * angle and the imaginary part is the sine of the angle. The returned
 * values are scaled by 2^16 for precision. The range for theta is
 * for -180 degrees to +180 degrees. Passed values outside this range are
 * converted before doing the actual calculation.
 */
#define cordic_calc_iq LINUX_I915_BACKPORT(cordic_calc_iq)
struct cordic_iq cordic_calc_iq(s32 theta);

#endif /* __CORDIC_H_ */
#endif /* LINUX_VERSION_CODE > KERNEL_VERSION(3,1,0)) */

#ifndef CORDIC_FLOAT
#define CORDIC_ANGLE_GEN	39797
#define CORDIC_PRECISION_SHIFT	16
#define CORDIC_NUM_ITER	(CORDIC_PRECISION_SHIFT + 2)

#define CORDIC_FIXED(X)	((s32)((X) << CORDIC_PRECISION_SHIFT))
#define CORDIC_FLOAT(X)	(((X) >= 0) \
		? ((((X) >> (CORDIC_PRECISION_SHIFT - 1)) + 1) >> 1) \
		: -((((-(X)) >> (CORDIC_PRECISION_SHIFT - 1)) + 1) >> 1))
#endif

#endif /* _BACKPORT_LINUX_CORDIC_H */
