// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_PRIVATE_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_PRIVATE_H_

#include <stddef.h>

#include <ktl/string_view.h>

class Handle;

struct InstrumentationDataVmo {
  // A descriptive string used in the kernel log message.
  ktl::string_view announce;

  // Name of the kind of data, matching the fuchsia.debugdata.Publish argument.
  ktl::string_view sink_name;

  // The contents are described as "up to N units", where N is bytes/scale.
  ktl::string_view units = "bytes";
  size_t scale = 1;

  Handle* handle = nullptr;
};

// profile.cc
InstrumentationDataVmo LlvmProfdataVmo();

// sancov.cc
InstrumentationDataVmo SancovGetPcVmo();
InstrumentationDataVmo SancovGetCountsVmo();

// phys.cc
InstrumentationDataVmo PhysSymbolizerVmo();
InstrumentationDataVmo PhysLlvmProfdataVmo();

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_PRIVATE_H_
