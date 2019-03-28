/******************************************************************************
 *
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include "iwl-drv.h"
#include "iwl-debug.h"
#if 0  // NEEDS_PORTING
#include "iwl-devtrace.h"
#endif  // NEEDS_PORTING

#define __iwl_fn(fn)						\
void __iwl_ ##fn(struct device *dev, const char *fmt, ...)	\
{								\
	struct va_format vaf = {				\
		.fmt = fmt,					\
	};							\
	va_list args1, args2;					\
								\
	va_start(args1, fmt);					\
	va_copy(args2, args1);					\
	vaf.va = &args2;					\
	dev_ ##fn(dev, "%pV", &vaf);				\
	va_end(args2);						\
	vaf.va = &args1;					\
	trace_iwlwifi_ ##fn(&vaf);				\
	va_end(args1);						\
}

void __iwl_err(struct device *dev, bool rfkill_prefix, bool trace_only,
		const char *fmt, ...)
{
#if 0  // NEEDS_PORTING
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	if (!trace_only) {
		va_list args2;

		va_copy(args2, args);
		vaf.va = &args2;
		if (rfkill_prefix)
			dev_err(dev, "(RFKILL) %pV", &vaf);
		else
			dev_err(dev, "%pV", &vaf);
		va_end(args2);
	}
	vaf.va = &args;
	trace_iwlwifi_err(&vaf);
	va_end(args);
#endif  // NEEDS_PORTING
}

#if defined(CPTCFG_IWLWIFI_DEBUG) || defined(CPTCFG_IWLWIFI_DEVICE_TRACING)
void __iwl_dbg(struct device *dev,
	       u32 level, bool limit, const char *function,
	       const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
#ifdef CPTCFG_IWLWIFI_DEBUG
	if (iwl_have_debug_level(level) &&
	    (!limit || net_ratelimit())) {
		va_list args2;

		va_copy(args2, args);
		vaf.va = &args2;
		dev_printk(KERN_DEBUG, dev, "%c %s %pV",
			   in_interrupt() ? 'I' : 'U', function, &vaf);
		va_end(args2);
	}
#endif
	vaf.va = &args;
	trace_iwlwifi_dbg(level, in_interrupt(), function, &vaf);
	va_end(args);
}
#endif

void __iwl_warn(struct device *dev, const char *fmt, ...) {}

void __iwl_info(struct device *dev, const char *fmt, ...) {}

void __iwl_crit(struct device *dev, const char *fmt, ...) {}
