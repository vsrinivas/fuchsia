// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_DYNAMIC_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_DYNAMIC_H_

#include <lib/stdcompat/span.h>

#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "layout.h"

namespace elfldltl {

using namespace std::literals::string_view_literals;

// elfldltl::DecodeDynamic does a single-pass decoding of the PT_DYNAMIC data
// by statically combining multiple "observer" objects.  Various observers are
// defined to collect different subsets of the dynamic linking metadata needed
// for different purposes.

// This represents a PT_DYNAMIC entry that's been matched to a specific tag.
// These types are used in the arguments to Observe callbacks; see below.
template <ElfDynTag Tag>
struct DynamicTagMatch {};

// This is the base class for Dynamic*Observer classes.
// Each Observer subclass should define these two methods:
//  * template <class DiagnosticsType, class Memory>
//    bool Observe(DiagnosticsType& diagnostics, Memory& memory,
//                 DynamicTagMatch<Tag> tag, Addr val);
//  * template <class DiagnosticsType, class Memory>
//    bool Finish(DiagnosticsType& diagnostics, Memory& memory);
// Observe will be called with each entry matching any Tag of the Tags... list.
// Then Finish will be called at the end of all entries unless processing was
// terminated early for some reason, in which case the observer object is
// usually going to be destroyed without checking its results.  Both return
// false if processing the dynamic section should be terminated early.

template <class Observer, ElfDynTag... Tags>
struct DynamicTagObserver {};

// This decodes a dynamic section by matching each entry against a list of
// observers.  Each observer should be of a subclass of DynamicTagObserver that
// indicates the tags it matches.  If any matching observer returns false then
// this stops processing early and returns false.  Otherwise, each observer's
// Finish method is called, stopping early if one returns false.
template <class DiagnosticsType, class Memory, class Dyn, class... Observers>
constexpr bool DecodeDynamic(DiagnosticsType&& diagnostics, Memory&& memory,
                             cpp20::span<const Dyn> dyn, Observers&&... observers) {
  // The span is an upper bound but the section is terminated by a null entry.
  for (const auto& entry : dyn) {
    // At the terminator entry, call each observer's Finish() method.
    if (entry.tag == ElfDynTag::kNull) {
      return (observers.Finish(diagnostics, memory) && ...);
    }

    // Present each entry to each matching observer (see below).
    if ((!DecodeDynamic(diagnostics, memory, entry, observers) || ...)) {
      return false;
    }
  }

  // This should never be reached.
  return diagnostics.FormatError("missing DT_NULL terminator in PT_DYNAMIC"sv);
}

// Match a single dynamic section entry against a single observer.  If the
// observer matches, its Observe overload for the matching tag is called.
// Returns the value of that call, or true if this observer didn't match.
template <class DiagnosticsType, class Memory, class Dyn, class Observer, ElfDynTag... Tags>
constexpr bool DecodeDynamic(DiagnosticsType&& diagnostics, Memory&& memory, const Dyn& entry,
                             DynamicTagObserver<Observer, Tags...>& observer) {
  bool ok = true;
  auto call_observer = [&](auto dt) {
    ok = static_cast<Observer&>(observer).Observe(diagnostics, memory, dt, entry.val);
    return true;
  };
  ((entry.tag == Tags && call_observer(DynamicTagMatch<Tags>{})) || ...);
  return ok;
}

// This is a very simple observer that rejects DT_TEXTREL.
class DynamicTextrelRejectObserver
    : public DynamicTagObserver<DynamicTextrelRejectObserver, ElfDynTag::kTextRel> {
 public:
  static constexpr std::string_view Message() { return "DT_TEXTREL not supported"sv; }

  template <class DiagnosticsType, class Memory, typename ValueType>
  constexpr bool Observe(DiagnosticsType& diagnostics, Memory& memory,
                         DynamicTagMatch<ElfDynTag::kTextRel> tag, ValueType val) {
    // If this is called at all, that's an error.
    return diagnostics.FormatError(Message());
  }

  template <class DiagnosticsType, class Memory>
  constexpr bool Finish(DiagnosticsType& diagnostics, Memory& memory) {
    // There is no state kept aside from in the diagnostics object, so nothing to do.
    return true;
  }
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_DYNAMIC_H_
