#ifndef __BACKPORT_H
#define __BACKPORT_H
#include <generated/autoconf.h>
#ifndef CONFIG_BACKPORT_INTEGRATE
#include <backport/autoconf.h>
#include <backport/backport_macro.h>
#endif
#include <linux/kconfig.h>

#ifndef __ASSEMBLY__
#define LINUX_I915_BACKPORT(__sym) i915bkpt_ ##__sym
#ifndef CONFIG_BACKPORT_INTEGRATE
#include <backport/checks.h>
#endif
#endif

#endif /* __BACKPORT_H */
