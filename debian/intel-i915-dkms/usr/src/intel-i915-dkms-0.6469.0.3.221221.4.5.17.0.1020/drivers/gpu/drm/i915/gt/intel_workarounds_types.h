/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#ifndef __INTEL_WORKAROUNDS_TYPES_H__
#define __INTEL_WORKAROUNDS_TYPES_H__

#include <linux/types.h>

#include "i915_reg_defs.h"

struct i915_whitelist_reg {
	i915_reg_t reg;
	u32 flags;
};

struct i915_wa {
	i915_reg_t	reg;
	u32		clr;
	u32		set;
	u32		read;
	bool		masked_reg;
};

struct i915_wa_list {
	const char	*name;
	const char	*engine_name;
	struct i915_wa	*list;
	unsigned int	count;
};

#endif /* __INTEL_WORKAROUNDS_TYPES_H__ */
