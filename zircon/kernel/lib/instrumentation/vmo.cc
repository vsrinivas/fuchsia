// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/instrumentation/vmo.h>
#include <lib/version.h>
#include <stdio.h>
#include <string.h>

#include <ktl/iterator.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_object_paged.h>

namespace {

// These are defined by the linker script.  When there is no such
// instrumentation data in this kernel build, they're equal.
extern "C" const uint8_t __llvm_profile_start[], __llvm_profile_end[];
extern "C" const uint8_t __llvm_profile_vmo_end[];
extern "C" const uint8_t __sancov_pc_table[], __sancov_pc_table_end[];
extern "C" const uint8_t __sancov_pc_table_vmo_end[];
extern "C" const uint8_t __sancov_pc_counts[], __sancov_pc_counts_end[];
extern "C" const uint8_t __sancov_pc_counts_vmo_end[];

constexpr struct Kind {
  const char* announce;
  const char* sink_name;
  const char* vmo_name;
  const uint8_t* start;
  const uint8_t* end;
  const uint8_t* vmo_end;
  size_t scale;
  const char* units;

  constexpr size_t content_size() const { return end - start; }
} kKinds[] = {
    // LLVM profile data.  When not compiled in, this will be a zero-length
    // anonymous VMO and userland will just ignore it.  But it's simpler to
    // keep the number of VMOs fixed in the ABI with userboot because the
    // way the build works, the userboot build is independent of different
    // kernel variants that might have things enabled or disabled.
    {"LLVM Profile", "llvm-profile", "data/zircon.elf.profraw",
     // Linker-generated symbols.
     __llvm_profile_start, __llvm_profile_end, __llvm_profile_vmo_end,
     // Units.
     1, "bytes"},

    // -fsanitizer-coverage=trace-pc-guard data.  Same story.
    {"SanitizerCoverage", "sancov",
     // The sancov tool matches "<binaryname>" to "<binaryname>.%u.sancov".
     "data/zircon.elf.1.sancov",
     // Linker-generated symbols.
     __sancov_pc_table, __sancov_pc_table_end, __sancov_pc_table_vmo_end,
     // Units.
     sizeof(uintptr_t), "PCs"},
    {"SanitizerCoverage Counts", "sancov-counts",
     // This follows the sancov PCs file name just for consistency.
     "data/zircon.elf.1.sancov-counts",
     // Linker-generated symbols.
     __sancov_pc_counts, __sancov_pc_counts_end, __sancov_pc_counts_vmo_end,
     // Units.
     sizeof(uint64_t), "counters"},

    // NOTE!  This element must be last.  This file contains logging text
    // with symbolizer markup that describes all the other data files.
    {{}, {}, "data/symbolizer.log", {}, {}, {}, {}, {}},
};

void PrintDumpfile(FILE* f, const Kind& k) {
  fprintf(f, "%s: {{{dumpfile:%s:%s}}} maximum %zu %s.\n", k.announce, k.sink_name, k.vmo_name,
          (k.end - k.start) / k.scale, k.units);
}

}  // namespace

decltype(InstrumentationData::instances_) InstrumentationData::instances_;

zx_status_t InstrumentationData::Create() {
  const auto& k = kKinds[which()];
  return VmObjectPaged::CreateFromWiredPages(k.start, k.vmo_end - k.start, false, &vmo_);
}

zx_status_t InstrumentationData::GetVmo(Handle** handle) {
  zx_rights_t rights;
  KernelHandle<VmObjectDispatcher> new_handle;
  zx_status_t status = VmObjectDispatcher::Create(vmo_, &new_handle, &rights);
  if (status == ZX_OK) {
    new_handle.dispatcher()->SetContentSize(kKinds[which()].content_size());
    *handle = Handle::Make(ktl::move(new_handle), rights & ~ZX_RIGHT_WRITE).release();
  }
  return status;
}

bool InstrumentationData::Publish(FILE* symbolizer) {
  if (vmo_->size() == 0) {
    return false;
  }

  const auto& k = kKinds[which()];

  // Set the name to expose the meaning of the VMO to userland.
  vmo_->set_name(k.vmo_name, strlen(k.vmo_name));

  if (symbolizer) {
    // Log the name that goes with the VMO.
    PrintDumpfile(stdout, k);
    PrintDumpfile(symbolizer, k);
  }

  return true;
}

zx_status_t InstrumentationData::GetVmos(Handle* handles[]) {
  // This object facilitates doing fprintf directly into the VMO representing
  // the symbolizer markup data file.  This gets the symbolizer context for the
  // kernel and then a dumpfile element for each VMO published.
  struct SymbolizerFile {
    fbl::RefPtr<VmObjectPaged>& vmo_ = instances_[kSymbolizer].vmo_;
    size_t pos_ = 0;
    FILE stream_{this};

    zx_status_t Create() {
      return VmObjectPaged::Create(PMM_ALLOC_FLAG_ANY, VmObjectPaged::kResizable, PAGE_SIZE, &vmo_);
    }

    int Write(ktl::string_view str) {
      zx_status_t status = vmo_->Write(str.data(), pos_, str.size());
      ZX_ASSERT(status == ZX_OK);
      pos_ += str.size();
      return static_cast<int>(str.size());
    }
  } symbolizer;
  zx_status_t status = symbolizer.Create();
  if (status != ZX_OK) {
    return status;
  }
  PrintSymbolizerContext(&symbolizer.stream_);

  bool any_published = false;
  for (auto& instance : instances_) {
    bool published = false;
    if (instance.which() == kSymbolizer) {
      // This is the last iteration, so everything has been published now.
      static_assert(kSymbolizer == ktl::size(instances_) - 1);
      if (any_published) {
        // Publish the symbolizer file.
        published = instance.Publish(nullptr);
      } else {
        // Nothing to publish, so zero out the symbolizer file VMO so it won't
        // be published either.
        status = instance.vmo_->Resize(0);
        ZX_ASSERT(status == ZX_OK);
      }
    } else {
      status = instance.Create();
      published = instance.Publish(&symbolizer.stream_);
    }
    if (status == ZX_OK) {
      status = instance.GetVmo(&handles[instance.which()]);
    }
    if (published) {
      any_published = true;
    } else {
      // The empty VMO doesn't need to be kept alive.
      instance.vmo_.reset();
    }
    if (status != ZX_OK) {
      return status;
    }
  }

  if (any_published) {
    // Finalize the official size of the symbolizer file.
    auto* dispatcher = handles[kSymbolizer]->dispatcher().get();
    auto vmo = DownCastDispatcher<VmObjectDispatcher>(dispatcher);
    vmo->SetContentSize(symbolizer.pos_);
  }

  // There's no need to keep the symbolizer file VMO alive if userland drops
  // it.  Its memory is not special and isn't used by the kernel directly.
  symbolizer.vmo_.reset();

  return ZX_OK;
}
