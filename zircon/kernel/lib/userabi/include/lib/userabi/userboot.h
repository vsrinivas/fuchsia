// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_USERBOOT_H_
#define ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_USERBOOT_H_

// This file specifies the private ABI shared between userboot and the kernel.
// That is, the contents of the message sent on userboot's bootstrap channel.

#include <lib/instrumentation/vmo.h>

#include <cstdint>

namespace userboot {

// This is only here for the count.  No userboot code cares which is which
// except that the full (default) variant is first and that kLastVdso (below)
// is correct.
enum class VdsoVariant { FULL, TEST1, TEST2, COUNT };

// The data of the bootstrap message is the kernel command line,
// as a sequence of '\0'-terminated words followed by a final '\0'.
// This is its maximum size.
constexpr uint32_t kCmdlineMax = 4096;

// The handles in the bootstrap message are as follows:
enum HandleIndex : uint32_t {
  // These describe userboot itself.
  kProcSelf,
  kVmarRootSelf,

  // Essential job and resource handles.
  kRootJob,
  kRootResource,
  kMmioResource,
  kIrqResource,

  // Essential VMO handles.
  kZbi,

  kFirstVdso,
  kLastVdso = kFirstVdso + static_cast<uint32_t>(VdsoVariant::COUNT) - 1,

  // These get passed along to userland to be recognized by ZX_PROP_NAME.
  // The remainder are VMO handles that userboot doesn't care about.
  kCrashlog,
  kFirstKernelFile = kCrashlog,

  kCounterNames,
  kCounters,
#if ENABLE_ENTROPY_COLLECTOR_TEST
  kEntropyTestData,
#endif

  kFirstInstrumentationData,
  kHandleCount = kFirstInstrumentationData + InstrumentationData::vmo_count()
};

}  // namespace userboot

#endif  // ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_USERBOOT_H_
