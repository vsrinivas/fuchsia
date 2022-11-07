// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "phys/elf-image.h"

#include <inttypes.h>
#include <lib/elfldltl/diagnostics.h>
#include <lib/elfldltl/dynamic.h>
#include <lib/elfldltl/link.h>
#include <lib/elfldltl/load.h>
#include <zircon/assert.h>
#include <zircon/limits.h>

#include <ktl/atomic.h>
#include <ktl/move.h>
#include <ktl/variant.h>
#include <phys/allocation.h>

#include <ktl/enforce.h>

namespace {

constexpr ktl::string_view kDiagnosticsPrefix = "Cannot load ELF image: ";

// TODO(mcgrathr): BFD ld produces a spurious empty .eh_frame with its own
// empty PT_LOAD segment. This is harmless enough to the actual layout,
// but triggers a FormatWarning.
#ifdef __clang__
auto GetDiagnostics() { return elfldltl::PanicDiagnostics(kDiagnosticsPrefix); }
#else
constexpr auto kPanicReport = elfldltl::PanicDiagnosticsReport(kDiagnosticsPrefix);
using DiagBase = elfldltl::Diagnostics<decltype(kPanicReport), elfldltl::DiagnosticsPanicFlags>;
struct NoWarnings : public DiagBase {
  constexpr NoWarnings() : DiagBase(kPanicReport) {}
  static constexpr auto FormatWarning = [](auto&&...) { return true; };
};
auto GetDiagnostics() { return NoWarnings(); }
#endif
}  // namespace

fit::result<ElfImage::Error> ElfImage::Init(ElfImage::BootfsDir dir, ktl::string_view name,
                                            bool relocated) {
  auto read_file = [this, &dir, name]() -> fit::result<Error> {
    if (auto found = dir.find(name); found != dir.end()) {
      // Singleton ELF file, no patches.
      dir.ignore_error();
      image_.set_image(found->data);
      return fit::ok();
    }

    BootfsDir subdir;
    if (auto result = dir.subdir(name); result.is_ok()) {
      subdir = ktl::move(result).value();
    } else {
      return result.take_error();
    }

    // Find the ELF file in the directory.
    auto it = subdir.find(kImageName);
    if (it == subdir.end()) {
      if (auto result = subdir.take_error(); result.is_error()) {
        return result.take_error();
      }
      return fit::error{Error{
          .reason = "ELF file not found in image directory"sv,
          .filename = kImageName,
      }};
    }
    subdir.ignore_error();
    image_.set_image(it->data);

    // Now find the code patches.
    patcher_.emplace();
    if (auto result = patcher_->Init(subdir); result.is_error()) {
      return result.take_error();
    }

    return fit::ok();
  };

  if (auto result = read_file(); result.is_error()) {
    return result;
  }

  auto diagnostics = GetDiagnostics();
  auto phdr_allocator = elfldltl::NoArrayFromFile<elfldltl::Elf<>::Phdr>();
  auto headers =
      elfldltl::LoadHeadersFromFile<elfldltl::Elf<>>(diagnostics, image_, phdr_allocator);
  auto [ehdr, phdrs] = *headers;

  ktl::optional<elfldltl::Elf<>::Phdr> relro, dynamic, interp;
  elfldltl::DecodePhdrs(
      diagnostics, phdrs, load_.GetPhdrObserver(ZX_PAGE_SIZE),
      elfldltl::PhdrFileNoteObserver(elfldltl::Elf<>(), image_,
                                     elfldltl::NoArrayFromFile<ktl::byte>(),
                                     elfldltl::ObserveBuildIdNote(build_id_)),
      elfldltl::PhdrSingletonObserver<elfldltl::Elf<>, elfldltl::ElfPhdrType::kRelro>(relro),
      elfldltl::PhdrDynamicObserver<elfldltl::Elf<>>(dynamic),
      elfldltl::PhdrInterpObserver<elfldltl::Elf<>>(interp));

  image_.set_base(load_.vaddr_start());
  entry_ = ehdr.entry;

  if (relocated) {
    // In the phys context, all the relocations are done in place before the
    // image is considered "loaded".  Update the load segments to indicate
    // RELRO protections have already been applied.
    load_.ApplyRelro(diagnostics, relro, ZX_PAGE_SIZE, true);
  }

  if (dynamic) {
    dynamic_ = *image_.ReadArray<elfldltl::Elf<>::Dyn>(
        dynamic->offset, dynamic->filesz / sizeof(elfldltl::Elf<>::Dyn));
  }

  if (interp) {
    auto chars = image_.ReadArrayFromFile<char>(interp->offset, elfldltl::NoArrayFromFile<char>(),
                                                interp->filesz);
    ZX_ASSERT_MSG(chars, "PT_INTERP has invalid offset range [%#" PRIx64 ", %#" PRIx64 ")",
                  interp->offset(), interp->offset() + interp->filesz());
    ZX_ASSERT_MSG(!chars->empty(), "PT_INTERP has zero filesz");
    ZX_ASSERT_MSG(chars->back() == '\0', "PT_INTERP missing NUL terminator");
    interp_.emplace(chars->data(), chars->size() - 1);
  }

  return fit::ok();
}

ktl::span<ktl::byte> ElfImage::GetBytesToPatch(const code_patching::Directive& patch) {
  ktl::span<ktl::byte> file = image_.image();
  ZX_ASSERT_MSG(patch.range_start >= image_.base() && file.size() >= patch.range_size &&
                    file.size() - patch.range_size >= patch.range_start - image_.base(),
                "Patch ID %#" PRIx32 " range [%#" PRIx64 ", %#" PRIx64
                ") is outside file bounds [%#" PRIx64 ", %#" PRIx64 ")",
                patch.id, patch.range_start, patch.range_start + patch.range_size, image_.base(),
                image_.base() + file.size());
  return file.subspan(patch.range_start - image_.base(), patch.range_size);
}

Allocation ElfImage::Load() {
  if (CanLoadInPlace()) {
    // TODO(fxbug.dev/113938): Could have a memalloc::Pool feature to
    // reclassify the memory range to the new type.
    LoadInPlace();
    return {};
  }

  fbl::AllocChecker ac;
  Allocation image =
      Allocation::New(ac, memalloc::Type::kPhysElf, load_.vaddr_size(), ZX_PAGE_SIZE);
  if (!ac.check()) {
    ZX_PANIC("cannot allocate phys ELF load image of %#zx bytes", load_.vaddr_size());
  }

  auto endof = [](const auto& last) { return last.offset() + last.filesz(); };
  const uint64_t load_size = ktl::visit(endof, load_.segments().back());

  ZX_ASSERT(load_size <= image.size_bytes());
  ZX_ASSERT(load_size <= image_.image().size_bytes());
  memcpy(image.get(), image_.image().data(), load_size);

  SetLoadAddress(reinterpret_cast<uintptr_t>(image.get()));

  return image;
}

void ElfImage::Relocate() {
  ZX_DEBUG_ASSERT(load_bias_);  // The load address has already been chosen.
  if (!dynamic_.empty()) {
    auto diagnostics = GetDiagnostics();
    elfldltl::RelocationInfo<elfldltl::Elf<>> reloc_info;
    elfldltl::DecodeDynamic(diagnostics, image_, dynamic_,
                            elfldltl::DynamicRelocationInfoObserver(reloc_info));
    ZX_ASSERT(reloc_info.rel_symbolic().empty());
    ZX_ASSERT(reloc_info.rela_symbolic().empty());
    bool relocated = elfldltl::RelocateRelative(image_, reloc_info, *load_bias_);
    ZX_ASSERT(relocated);

    // Make sure everything is written before the image is used as code.
    ktl::atomic_signal_fence(ktl::memory_order_seq_cst);
  }
}

void ElfImage::AssertInterp(ktl::string_view prefix, ktl::string_view interp) {
  ZX_ASSERT_MSG(interp_, "%.*s: ELF image has no PT_INTERP (expected %.*s)",
                static_cast<int>(prefix.size()), prefix.data(),  //
                static_cast<int>(interp.size()), interp.data());
  ZX_ASSERT_MSG(*interp_ == interp, "%.*s: ELF image PT_INTERP %.*s != expected %.*s)",
                static_cast<int>(prefix.size()), prefix.data(),      //
                static_cast<int>(interp_->size()), interp_->data(),  //
                static_cast<int>(interp.size()), interp.data());
}
