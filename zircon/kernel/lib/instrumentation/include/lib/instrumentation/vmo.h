// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_VMO_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_VMO_H_

// This header is also used in userboot just for vmo_count().

#include <stdint.h>
#include <zircon/types.h>

class Handle;

class InstrumentationData {
 public:
  static constexpr uint32_t vmo_count() { return kVmoCount; }

  static zx_status_t GetVmos(Handle* handles[]);

 private:
  enum Vmo : uint32_t {
    kSymbolizerVmo,
    kPhysSymbolizerVmo,
    kPhysLlvmProfdataVmo,
    kLlvmProfdataVmo,
    kSancovVmo,
    kSancovCountsVmo,
    kVmoCount,
  };
};

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_VMO_H_
