// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_VMO_H_
#define ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_VMO_H_

#include <zircon/types.h>

#include <cstdint>

// The header is also used in userboot just for vmo_count().
#ifdef _KERNEL
#include <fbl/ref_ptr.h>
#include <ktl/array.h>
#include <vm/vm_object_paged.h>
#endif

class Handle;
class VmObject;

class InstrumentationData {
 public:
  static constexpr uint32_t vmo_count() { return kVmoCount; }

  static zx_status_t GetVmos(Handle* handles[]);

 private:
  enum Vmo : uint32_t {
    kLlvmProfileVmo,
    kSancovVmo,
    kSancovCountsVmo,
    kSymbolizer,  // Must be last.
    kVmoCount,
  };

#ifdef _KERNEL
  // These live forever to keep a permanent reference to the VMO so that the
  // memory always remains valid, even if userspace closes the last handle.
  static ktl::array<InstrumentationData, kVmoCount> instances_;
  fbl::RefPtr<VmObjectPaged> vmo_;

  // The only instances possible are the static ones.
  InstrumentationData() = default;
  friend decltype(instances_);  // Let (only) it call the constructor.

  InstrumentationData(const InstrumentationData&) = delete;
  InstrumentationData& operator=(const InstrumentationData&) = delete;

  Vmo which() const { return static_cast<Vmo>(this - &instances_[0]); }

  zx_status_t Create();
  bool Publish(FILE*);
  zx_status_t GetVmo(Handle**);
#endif
};

#endif  // ZIRCON_KERNEL_LIB_INSTRUMENTATION_INCLUDE_LIB_INSTRUMENTATION_VMO_H_
