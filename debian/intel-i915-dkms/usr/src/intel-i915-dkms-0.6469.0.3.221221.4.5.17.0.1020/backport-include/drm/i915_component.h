#ifndef __BACKPORT_I915_COMPONENT_H
#define __BACKPORT_I915_COMPONENT_H
#include_next <drm/i915_component.h>

#define i915_component_type LINUX_I915_BACKPORT(i915_component_type)
#define I915_COMPONENT_AUDIO LINUX_I915_BACKPORT(I915_COMPONENT_AUDIO)
#define I915_COMPONENT_HDCP LINUX_I915_BACKPORT(I915_COMPONENT_HDCP)
#define I915_COMPONENT_PXP LINUX_I915_BACKPORT(I915_COMPONENT_PXP)
#define I915_COMPONENT_IAF LINUX_I915_BACKPORT(I915_COMPONENT_IAF)
enum i915_component_type {
	I915_COMPONENT_AUDIO = 1,
	I915_COMPONENT_HDCP,
	I915_COMPONENT_PXP,
	I915_COMPONENT_IAF,
};

#endif /* __BACKPORT_I915_COMPONENT_H */
