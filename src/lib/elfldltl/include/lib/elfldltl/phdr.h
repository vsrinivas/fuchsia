// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_

#include <lib/stdcompat/span.h>

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
  auto check_flags = [&](std::string_view warning) {
    if ((phdr.flags() & ~kKnownFlags) != 0) {
      ok = diag.FormatWarning(warning);
    }
    return ok;
  };
  auto call_observer = [&](auto type) {
    ok = static_cast<Observer&>(observer).Observe(diag, type, phdr);
    return ok;
  };

  ((phdr.type == Type &&                                      //
    check_flags(internal::PhdrError<Type>::kUnknownFlags) &&  //
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

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_
