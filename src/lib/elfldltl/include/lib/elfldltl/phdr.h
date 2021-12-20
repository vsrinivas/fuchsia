// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_

#include <lib/stdcompat/span.h>

#include <optional>
#include <string_view>

#include "constants.h"

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
  bool ok = true;
  auto call_observer = [&](auto type) {
    ok = static_cast<Observer&>(observer).Observe(diag, type, phdr);
    return true;
  };
  ((phdr.type == Type && call_observer(PhdrTypeMatch<Type>{})) || ...);
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

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_PHDR_H_
