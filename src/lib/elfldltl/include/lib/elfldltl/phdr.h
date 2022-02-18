// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_

#include <lib/stdcompat/bit.h>
#include <lib/stdcompat/span.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>

#include "constants.h"
#include "internal/phdr-error.h"

namespace elfldltl {

using namespace std::literals::string_view_literals;

// elfldltl::DecodePhdrs does a single-pass decoding of a program
// header table by statically combining multiple "observer" objects.  Various
// observers are defined to collect different subsets of the segment metadata
// needed for different purposes.

// This represents a program header entry that's been matched to a specific
// segment type. These types are used in the arguments to Observe callbacks;
// see below.
template <ElfPhdrType Type>
struct PhdrTypeMatch {};

// This is the base class for Phdr*Observer classes.
// Each Observer subclass should define these two methods:
//  * template <class Diag, typename Phdr>
//    bool Observe(Diag& diag, PhdrTypeMatch<Type> type, const Phdr& val);
//  * template <class Diag>
//    bool Finish(Diag& diag);
// Observe will be called with each entry matching any Type of the Types...
// list. Then Finish will be called at the end of all entries unless processing
// was terminated early for some reason, in which case the observer object is
// usually going to be destroyed without checking its results.  Both return
// false if processing the phdr table should be terminated early.
//
// In practice, a given observer will correspond to a single ElfPhdrType;
// however, the parameter is kept variadic for consistency with other APIs in
// the library.
template <class Observer, ElfPhdrType... Types>
struct PhdrObserver {};

// This decodes a program header table by matching each entry against a list of
// observers.  Each observer should be of a subclass of PhdrObserver that
// indicates the segment type it matches.  If any matching observer returns
// false then this stops processing early and returns false.  Otherwise, each
// observer's Finish method is called, stopping early if one returns false.
template <class Diag, class Phdr, size_t N, class... Observers>
constexpr bool DecodePhdrs(Diag&& diag, cpp20::span<const Phdr, N> phdrs,
                           Observers&&... observers) {
  for (const Phdr& phdr : phdrs) {
    if ((!DecodePhdr(diag, phdr, observers) || ...)) {
      return false;
    }
  }
  return (observers.Finish(diag) && ...);
}

// Match a single program header against a single observer.  If the
// observer matches, its Observe overload for the matching tag is called.
// Returns the value of that call, or true if this observer didn't match.
template <class Diag, class Phdr, class Observer, ElfPhdrType... Type>
constexpr bool DecodePhdr(Diag&& diag, const Phdr& phdr,
                          PhdrObserver<Observer, Type...>& observer) {
  constexpr auto kKnownFlags = Phdr::kRead | Phdr::kWrite | Phdr::kExecute;

  bool ok = true;
  auto check_header = [&](auto error) {
    using Error = decltype(error);

    if ((phdr.flags() & ~kKnownFlags) != 0) {
      if (ok = diag.FormatWarning(Error::kUnknownFlags); !ok) {
        return false;
      }
    }

    // An `align` of 0 signifies no alignment constraints, which in practice
    // means an alignment of 1.
    auto align = phdr.align() > 0 ? phdr.align() : 1;
    if (!cpp20::has_single_bit(align)) {
      if (ok = diag.FormatError(Error::kBadAlignment); !ok) {
        return false;
      }
    }

    // While not a general spec'd constraint, this is the case in practice:
    // either this a explicit requirement or the stronger constraint of
    // `p_offset` and `p_vaddr` being `p_align`-aligned (e.g., zero) is
    // expected to hold.
    if (phdr.offset() % align != phdr.vaddr() % align) {
      if (ok = diag.FormatError(Error::kOffsetNotEquivVaddr); !ok) {
        return false;
      }
    }

    return true;
  };
  auto call_observer = [&](auto type) {
    ok = static_cast<Observer&>(observer).Observe(diag, type, phdr);
    return ok;
  };

  ((phdr.type == Type && check_header(internal::PhdrError<Type>{}) &&
    call_observer(PhdrTypeMatch<Type>{})) ||
   ...);
  return ok;
}

template <class Elf>
class PhdrNullObserver : public PhdrObserver<PhdrNullObserver<Elf>, ElfPhdrType::kNull> {
 public:
  using Phdr = typename Elf::Phdr;

  template <class Diag>
  constexpr bool Observe(Diag& diag, PhdrTypeMatch<ElfPhdrType::kNull> type, const Phdr& phdr) {
    return diag.FormatWarning("PT_NULL header encountered");
  }

  template <class Diag>
  constexpr bool Finish(Diag& diag) {
    return true;
  }
};

// A class of observer corresponding to the simpler segment metadata types:
// it merely stores any program header that it sees at the provided reference,
// complaining if it observes more than one segment of the same type.
template <class Elf, ElfPhdrType Type>
class PhdrSingletonObserver : public PhdrObserver<PhdrSingletonObserver<Elf, Type>, Type> {
 public:
  using Phdr = typename Elf::Phdr;

  explicit PhdrSingletonObserver(std::optional<Phdr>& phdr) : phdr_(phdr) {}

  template <class Diag>
  constexpr bool Observe(Diag& diag, PhdrTypeMatch<Type> type, const Phdr& phdr) {
    // Warning, since a wrong PHDRS clause in a linker script could cause
    // this and be harmless in practice rather than indicating a linker bug
    // or corrupted data.
    if (phdr_ && !diag.FormatWarning(internal::PhdrError<Type>::kDuplicateHeader)) {
      return false;
    }
    phdr_ = phdr;
    return true;
  }

  template <class Diag>
  constexpr bool Finish(Diag& diag) {
    return true;
  }

 protected:
  std::optional<Phdr>& phdr() { return phdr_; }

 private:
  std::optional<Phdr>& phdr_;
};

// Observes PT_GNU_STACK metadata. If CanBeExecutable is true, then the absence
// of any PT_GNU_STACK header is conventionally taken to mean that the stack is
// executable; if CanBeExecutable is false, then Finish() gives an error if no
// header is found or if it reports that the stack is executable (i.e., if
// PF_X is set).
template <class Elf, bool CanBeExecutable = false>
class PhdrStackObserver : public PhdrSingletonObserver<Elf, ElfPhdrType::kStack> {
 private:
  using Base = PhdrSingletonObserver<Elf, ElfPhdrType::kStack>;
  using size_type = typename Elf::size_type;
  using Phdr = typename Elf::Phdr;

 public:
  // There is only one constructor, but its signature is dependent on
  //`CanBeExecutable`.

  template <bool E = CanBeExecutable, typename = std::enable_if_t<!E>>
  explicit PhdrStackObserver(std::optional<size_type>& size) : Base(phdr_), size_(size) {}

  template <bool E = CanBeExecutable, typename = std::enable_if_t<E>>
  PhdrStackObserver(std::optional<size_type>& size, bool& executable)
      : Base(phdr_), size_(size), executable_(executable) {}

  template <class Diag>
  constexpr bool Finish(Diag& diag) {
    using namespace std::literals::string_view_literals;

    if (!phdr_) {
      if constexpr (CanBeExecutable) {
        executable_ = true;
        return true;
      } else {
        return diag.FormatError("executable stack not supported: PT_GNU_STACK header required"sv);
      }
    }

    const auto flags = phdr_->flags();
    if ((flags & Phdr::kRead) == 0 &&
        !diag.FormatError("stack is not readable: PF_R is not set"sv)) {
      return false;
    }
    if ((flags & Phdr::kWrite) == 0 &&
        !diag.FormatError("stack is not writable: PF_W is not set"sv)) {
      return false;
    }

    if constexpr (CanBeExecutable) {
      executable_ = (flags & Phdr::kExecute) != 0;
    } else {
      if ((flags & Phdr::kExecute) != 0 &&
          !diag.FormatError("executable stack not supported: PF_X is set"sv)) {
        return false;
      }
    }

    if (phdr_->memsz != size_type{0}) {
      size_ = phdr_->memsz;
    }

    return true;
  }

 private:
  struct Empty {};

  std::optional<Phdr> phdr_;
  std::optional<size_type>& size_;
  [[no_unique_address]] std::conditional_t<CanBeExecutable, bool&, Empty> executable_;
};

// A generic metadata, singleton observer that validates constraints around
// sizes, offset, address, and segment entry type.
template <class Elf, ElfPhdrType Type, typename EntryType = std::byte>
class PhdrMetadataObserver : public PhdrSingletonObserver<Elf, Type> {
 private:
  using Base = PhdrSingletonObserver<Elf, Type>;
  using Phdr = typename Elf::Phdr;

 public:
  using Base::Base;

  template <class Diag>
  constexpr bool Finish(Diag& diag) {
    using PhdrError = internal::PhdrError<Type>;

    const std::optional<Phdr>& phdr = this->phdr();
    if (!phdr) {
      return true;
    }

    if (alignof(EntryType) > phdr->align() &&  //
        !diag.FormatError(PhdrError::kIncompatibleEntryAlignment)) {
      return false;
    }

    // Note that `p_vaddr % p_align == 0` implies that
    // `p_offset % p_align == 0` by virtue of the general equivalence check
    // made in DecodePhdrs().
    if (phdr->vaddr() % phdr->align() != 0 &&  //
        !diag.FormatError(PhdrError::kUnalignedVaddr)) {
      return false;
    }

    if (phdr->memsz != phdr->filesz &&  //
        !diag.FormatError(PhdrError::kFileszNotEqMemsz)) {
      return false;
    }

    if (phdr->filesz() % sizeof(EntryType) != 0 &&  //
        !diag.FormatError(PhdrError::kIncompatibleEntrySize)) {
      return false;
    }

    return true;
  }
};

template <class Elf>
using PhdrDynamicObserver = PhdrMetadataObserver<Elf, ElfPhdrType::kDynamic, typename Elf::Dyn>;

template <class Elf>
using PhdrInterpObserver = PhdrMetadataObserver<Elf, ElfPhdrType::kInterp>;

template <class Elf>
using PhdrEhFrameHdrObserver = PhdrMetadataObserver<Elf, ElfPhdrType::kEhFrameHdr>;

// PT_LOAD validation policy. Subsequent values extend previous ones.
enum class PhdrLoadPolicy {
  // Universal checks for all phdrs are made here (beyond universal checks
  // already made in `DecodePhdr()`):
  // * `p_align` is runtime page-aligned.
  // * `p_memsz >= p_filesz`;
  // * `p_align`-aligned memory ranges (`[p_vaddr, p_vaddr + p_memsz)`) do
  //   not overlap and increase monotonically.
  //
  // Underspecified, pathological cases like where `p_vaddr + p_memsz` or
  // `p_offset + p_filesz` overflow are checked as well.
  kBasic,

  // kFileRangeMonotonic further asserts that
  // * `p_align`-aligned file offset ranges (`[p_offset, p_offset + p_filesz)`)
  //   do not overlap and increase monotonically.
  //
  // This condition is meaningful for an ELF loader because if a writable
  // segment overlaps with any other segment, then one needs to map the former
  // to a COW copy of that part of the file rather than reusing the file data
  // directly (assuming one cares to not mutate the latter segment).
  kFileRangeMonotonic,

  // kContiguous further asserts that there is maximal 'contiguity' in the file
  // and memory layouts:
  // * `p_align`-aligned memory ranges are contiguous
  // * `p_align`-aligned file offset ranges are contiguous
  // * The first `p_offset` lies in the first page.
  // The first two conditions ensure that the unused space between ranges is
  // minimal, and permit the ELF file to be loaded as whole. Moreover, when
  // loading as a whole, the last condition ensures minimality in the unused
  // space in between the ELF header and the first `p_offset`.
  kContiguous,
};

// A PT_LOAD observer for a given metadata policy.
template <class Elf, PhdrLoadPolicy Policy = PhdrLoadPolicy::kBasic>
class PhdrLoadObserver : public PhdrObserver<PhdrLoadObserver<Elf, Policy>, ElfPhdrType::kLoad> {
 private:
  using Base = PhdrSingletonObserver<Elf, ElfPhdrType::kStack>;
  using Phdr = typename Elf::Phdr;
  using size_type = typename Elf::size_type;

 public:
  // `vaddr_start` and `vaddr_size` are updated to track the size of the
  // page-aligned memory image throughout observation.
  PhdrLoadObserver(size_type page_size, size_type& vaddr_start, size_type& vaddr_size)
      : vaddr_start_(vaddr_start), vaddr_size_(vaddr_size), page_size_(page_size) {
    ZX_ASSERT(cpp20::has_single_bit(page_size));
    vaddr_start_ = 0;
    vaddr_size_ = 0;
    ZX_DEBUG_ASSERT(NoHeadersSeen());
  }

  template <class Diag>
  constexpr bool Observe(Diag& diag, PhdrTypeMatch<ElfPhdrType::kLoad> type, const Phdr& phdr) {
    using namespace std::literals::string_view_literals;

    // If `p_align` is not page-aligned, then this file cannot be loaded through
    // normal memory mapping.
    if (0 < phdr.align() && phdr.align() < page_size_ &&
        !diag.FormatError("PT_LOAD's `p_align` is not page-aligned"sv)) {
      return false;
    }

    if (phdr.memsz() == 0 && !diag.FormatWarning("PT_LOAD has `p_memsz == 0`"sv)) {
      return false;
    }

    if (phdr.memsz() < phdr.filesz() && !diag.FormatError("PT_LOAD has `p_memsz < p_filez`"sv)) {
      return false;
    }

    // A `p_align` of 0 signifies no alignment constraints. So that we can
    // uniformally perform the usual alignment arithmetic below, we convert
    // such a value to 1, which has the same intended effect.
    auto align = std::max<size_type>(phdr.align(), 1);

    // Technically, having `p_vaddr + p_memsz` or `p_offset + p_filesz` be
    // exactly 2^64 is kosher per the ELF spec; however, such an ELF is not
    // usable, so we conventionally error out here along with at the usual
    // overflow scenarios.
    constexpr auto kMax = std::numeric_limits<size_type>::max();
    if (phdr.memsz() > kMax - phdr.vaddr()) [[unlikely]] {
      return diag.FormatError("PT_LOAD has overflowing `p_vaddr + p_memsz`"sv);
    }
    if (phdr.vaddr() + phdr.memsz() > kMax - align + 1) [[unlikely]] {
      return diag.FormatError("PT_LOAD has overflowing `p_align`-aligned `p_vaddr + p_memsz`"sv);
    }
    if (phdr.filesz() > kMax - phdr.offset()) [[unlikely]] {
      return diag.FormatError("PT_LOAD has overflowing `p_offset + p_filesz`"sv);
    }
    if (phdr.offset() + phdr.filesz() > kMax - align + 1) [[unlikely]] {
      return diag.FormatError("PT_LOAD has overflowing `p_align`-aligned `p_offset + p_filesz`"sv);
    }

    if (NoHeadersSeen()) {
      if constexpr (Policy == PhdrLoadPolicy::kContiguous) {
        if (phdr.offset() >= page_size_ &&
            !diag.FormatError("first PT_LOAD's `p_offset` does not lie within the first page"sv))
            [[unlikely]] {
          return false;
        }
      }

      vaddr_start_ = AlignDown(phdr.vaddr(), page_size_);
      vaddr_size_ = AlignUp(phdr.vaddr() + phdr.memsz(), page_size_) - vaddr_start_;
      UpdateHighWatermarks(phdr);
      return true;
    }

    if (AlignDown(phdr.vaddr(), align) < high_memory_watermark_) [[unlikely]] {
      return diag.FormatError(
          "PT_LOAD has `p_align`-aligned memory ranges that overlap or do not increase monotonically"sv);
    }

    if constexpr (kTrackFileOffsets) {
      if (AlignDown(phdr.offset(), align) < high_file_watermark_) [[unlikely]] {
        return diag.FormatError(
            "PT_LOAD has `p_align`-aligned file offset ranges that overlap or do not increase monotonically"sv);
      }
    }

    if constexpr (Policy == PhdrLoadPolicy::kContiguous) {
      if (AlignDown(phdr.vaddr(), align) != high_memory_watermark_) [[unlikely]] {
        return diag.FormatError(
            "PT_LOAD has `p_align`-aligned memory ranges that are not contiguous"sv);
      }

      if (AlignDown(phdr.offset(), align) != high_file_watermark_) [[unlikely]] {
        return diag.FormatError(
            "PT_LOAD has `p_align`-aligned file offset ranges that are not contiguous"sv);
      }
    }

    vaddr_size_ = AlignUp(phdr.vaddr() + phdr.memsz(), page_size_) - vaddr_start_;
    UpdateHighWatermarks(phdr);
    return true;
  }

  template <class Diag>
  constexpr bool Finish(Diag& diag) {
    return true;
  }

 private:
  struct Empty {};

  // Whether the given policy requires tracking file offsets.
  static constexpr bool kTrackFileOffsets = Policy != PhdrLoadPolicy::kBasic;

  static constexpr size_type AlignUp(size_type value, size_type alignment) {
    return (value + (alignment - 1)) & -alignment;
  }

  static constexpr size_type AlignDown(size_type value, size_type alignment) {
    return value & -alignment;
  }

  // Whether any PT_LOAD header have yet been observed.
  bool NoHeadersSeen() const { return vaddr_start_ == 0 && vaddr_size_ == 0; }

  void UpdateHighWatermarks(const Phdr& phdr) {
    auto align = std::max<size_type>(phdr.align(), 1);  // As above.
    high_memory_watermark_ = AlignUp(phdr.vaddr() + phdr.memsz(), align);

    if constexpr (kTrackFileOffsets) {
      high_file_watermark_ = AlignUp(phdr.offset() + phdr.filesz(), align);
    }
  }

  // The total, observed page-aligned load segment range in memory.
  size_type& vaddr_start_;
  size_type& vaddr_size_;

  // System page size.
  const size_type page_size_;

  // The highest `p_align`-aligned address and offset seen thus far.
  size_type high_memory_watermark_;
  [[no_unique_address]] std::conditional_t<kTrackFileOffsets, size_type, Empty>
      high_file_watermark_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_
