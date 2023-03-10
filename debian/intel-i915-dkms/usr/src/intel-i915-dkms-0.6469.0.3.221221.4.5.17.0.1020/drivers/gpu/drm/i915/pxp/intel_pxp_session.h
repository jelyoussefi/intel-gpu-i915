/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_SESSION_H__
#define __INTEL_PXP_SESSION_H__

#include <linux/types.h>

struct intel_pxp;

#ifdef CPTCFG_DRM_I915_PXP
void intel_pxp_session_management_init(struct intel_pxp *pxp);
#else
static inline void intel_pxp_session_management_init(struct intel_pxp *pxp)
{
}
#endif

#endif /* __INTEL_PXP_SESSION_H__ */
