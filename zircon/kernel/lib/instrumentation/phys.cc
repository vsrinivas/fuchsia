// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/llvm-profdata/llvm-profdata.h>
#include <zircon/assert.h>

#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/span.h>
#include <object/vm_object_dispatcher.h>
#include <phys/handoff.h>
#include <vm/vm_object_paged.h>

#include "private.h"

namespace {

constexpr ktl::string_view kSymbolizerName = "data/phys/symbolizer.log";
constexpr ktl::string_view kLlvmProfdataName = "data/phys/physboot.profraw";

Handle* MakePhysVmo(ktl::span<const ktl::byte> dump, ktl::string_view vmo_name) {
  if (dump.empty()) {
    return nullptr;
  }

  // Create a VMO to hold the whole dump.
  fbl::RefPtr<VmObjectPaged> vmo;
  zx_status_t status = VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, 0, dump.size_bytes(), &vmo);
  ZX_ASSERT(status == ZX_OK);

  status = vmo->Write(dump.data(), 0, dump.size_bytes());
  ZX_ASSERT(status == ZX_OK);

  zx_rights_t rights;
  KernelHandle<VmObjectDispatcher> handle;
  status =
      VmObjectDispatcher::Create(ktl::move(vmo), dump.size_bytes(),
                                 VmObjectDispatcher::InitialMutability::kMutable, &handle, &rights);
  ZX_ASSERT(status == ZX_OK);
  handle.dispatcher()->set_name(vmo_name.data(), vmo_name.size());
  return Handle::Make(ktl::move(handle), rights & ~ZX_RIGHT_WRITE).release();
}

}  // namespace

InstrumentationDataVmo PhysSymbolizerVmo() {
  ktl::string_view log = gPhysHandoff->instrumentation.symbolizer_log.get();
  ktl::span log_bytes = ktl::as_bytes(ktl::span(log.data(), log.size()));
  return {.handle = MakePhysVmo(log_bytes, kSymbolizerName)};
}

InstrumentationDataVmo PhysLlvmProfdataVmo() {
  ktl::span profdata_bytes = gPhysHandoff->instrumentation.llvm_profdata.get();
  return {
      .announce = LlvmProfdata::kAnnounce,
      .sink_name = LlvmProfdata::kDataSinkName,
      .handle = MakePhysVmo(profdata_bytes, kLlvmProfdataName),
  };
}
