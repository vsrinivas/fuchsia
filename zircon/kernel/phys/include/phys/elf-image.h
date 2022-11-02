// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ELF_IMAGE_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ELF_IMAGE_H_

#include <lib/code-patching/code-patching.h>
#include <lib/elfldltl/load.h>
#include <lib/elfldltl/memory.h>
#include <lib/elfldltl/note.h>
#include <lib/elfldltl/static-vector.h>
#include <lib/fit/result.h>
#include <lib/zbitl/items/bootfs.h>
#include <lib/zbitl/view.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <ktl/array.h>
#include <ktl/byte.h>
#include <ktl/move.h>
#include <ktl/optional.h>
#include <ktl/span.h>
#include <ktl/string_view.h>
#include <ktl/type_traits.h>

#include "allocation.h"

class ElfImage {
 public:
  static constexpr ktl::string_view kImageName = "image.elf";

  static constexpr size_t kMaxLoad = 4;  // RODATA, CODE, RELRO, DATA

  using LoadInfo = elfldltl::LoadInfo<elfldltl::Elf<>, elfldltl::StaticVector<kMaxLoad>::Container,
                                      elfldltl::PhdrLoadPolicy::kContiguous>;

  using BootfsDir = zbitl::BootfsView<ktl::span<ktl::byte>>;
  using Error = BootfsDir::Error;

  // An ELF image is found at "dir/name". That can be an ELF file or a subtree.
  // The subtree should contain "image.elf", "code-patches.bin", etc.  A
  // singleton file will be treated as the image with no patches to apply.
  fit::result<Error> Init(BootfsDir dir, ktl::string_view name, bool relocated);

  LoadInfo& load() { return load_; }
  const LoadInfo& load() const { return load_; }

  size_t size_bytes() const { return image_.image().size_bytes(); }

  uint64_t entry() const {
    ZX_DEBUG_ASSERT(load_bias_);
    return entry_ + *load_bias_;
  }

  bool has_patches() const { return !patches().empty(); }

  // The template parameter must be an `enum class Id : uint32_t` type.
  // Calls the callback as fit::result<Error>(code_patching::Patcher&,
  // Id, ktl::span<ktl::byte>) for each patch in the file.
  template <typename Id, typename Callback>
  fit::result<Error> ForEachPatch(Callback&& callback) {
    static_assert(ktl::is_enum_v<Id>);
    static_assert(ktl::is_same_v<uint32_t, ktl::underlying_type_t<Id>>);
    static_assert(ktl::is_invocable_r_v<fit::result<Error>, Callback, code_patching::Patcher&, Id,
                                        ktl::span<ktl::byte>>);
    for (const code_patching::Directive& patch : patches()) {
      ktl::span<ktl::byte> bytes = GetBytesToPatch(patch);
      auto result = callback(*patcher_, static_cast<Id>(patch.id), bytes);
      if (result.is_error()) {
        return result.take_error();
      }
    }
    return fit::ok();
  }

  // Set the virtual address where the image will be loaded.
  void SetLoadAddress(uint64_t address) {
    ZX_DEBUG_ASSERT(!load_bias_);
    ZX_ASSERT(address % ZX_PAGE_SIZE == 0);
    load_bias_ = address - load_.vaddr_start();
  }

  // Return true if the memory within the BOOTFS image for this file is
  // sufficient to be used in place as the load image.
  bool CanLoadInPlace() const {
    return load_.vaddr_size() <= ZBI_BOOTFS_PAGE_ALIGN(image_.image().size_bytes());
  }

  // Use the address of the file image inside the BOOTFS as the load address.
  void LoadInPlace() {
    uintptr_t address = reinterpret_cast<uintptr_t>(image_.image().data());
    SetLoadAddress(address);
  }

  // Load in place if possible, or else copy into a new Allocation.
  // On return, SetLoadAddress has been called.
  // The Allocation returned is null if LoadInPlace was used.
  Allocation Load();

  // Apply relocations to the image in place after setting the load address.
  void Relocate();

  // Call the image's entry point as a function type F.
  template <typename F, typename... Args>
  ktl::invoke_result_t<F*, Args...> Call(Args&&... args) const {
    static_assert(ktl::is_function_v<F>);
    F* fnptr = reinterpret_cast<F*>(static_cast<uintptr_t>(entry()));
    return (*fnptr)(ktl::forward<Args>(args)...);
  }

  // Call the image's entry point as a [[noreturn]] function type F.
  template <typename F, typename... Args>
  [[noreturn]] void Handoff(Args&&... args) const {
    Call<void(Args...)>(ktl::forward<Args>(args)...);
    ZX_PANIC("ELF image entry point returned!");
  }

 private:
  ktl::span<const code_patching::Directive> patches() const {
    if (patcher_) {
      return patcher_->patches();
    }
    return {};
  }

  ktl::span<ktl::byte> GetBytesToPatch(const code_patching::Directive& patch);

  elfldltl::DirectMemory image_{{}};
  LoadInfo load_;
  uint64_t entry_ = 0;
  ktl::span<const elfldltl::Elf<>::Dyn> dynamic_;
  ktl::optional<elfldltl::ElfNote> build_id_;
  ktl::optional<ktl::string_view> interp_;
  ktl::optional<code_patching::Patcher> patcher_;
  ktl::optional<uint64_t> load_bias_;
};

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_ELF_IMAGE_H_
