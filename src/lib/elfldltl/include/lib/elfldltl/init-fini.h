// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INIT_FINI_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INIT_FINI_H_

#include <lib/stdcompat/span.h>

#include <optional>

namespace elfldltl {

// A module initializer or finalizer function has type void().
using InitFiniFunction = void();

// This represents the information about either initializers or finalizers for
// one ELF module.  Two separate InitFiniInfo objects are used for a module's
// initializers and finalizers.
//
// This is normally populated by a call to elfldltl::DecodeDynamic using an
// elfldltl::DynamicInitObserver or elfldltl::DynamicFiniObserver observer.
//
// The VisitInit or VisitFini methods provide general ordered access to the
// function addresses in each list.  The correct method should be used for each
// kind of list to get the appropriate ordering of elements.
//
// The CallInit and CallFini methods directly call each function in order, for
// immediate in-process uses.
template <class Elf>
struct InitFiniInfo {
 public:
  using Addr = typename Elf::Addr;
  using size_type = typename Elf::size_type;

  // An array of function pointers, in the .init_array or .fini_array section,
  // which is normally part of the RELRO segment.  So the pointers here are
  // unrelocated in the file, but dynamic relocation records apply simple
  // fixup.  As this points directly into the load image in the Memory object,
  // if that image is being relocated in place, then these values will be
  // absolute function pointers after relocation.  If the original file data
  // (or the load image before relocation) is being read, the these addresses
  // need the load bias added.
  constexpr cpp20::span<const Addr> array() const { return array_; }

  // A single function pointer, from the legacy DT_INIT or DT_FINI entry.  This
  // is not contiguous with the array and is stored separately in the ELF
  // headers where no relocation records apply.  So this address always needs
  // the load bias added to yield a runtime function pointer.
  constexpr std::optional<Addr> legacy() const { return legacy_; }

  constexpr InitFiniInfo& set_array(cpp20::span<const Addr> array) {
    array_ = array;
    return *this;
  }

  constexpr InitFiniInfo& set_legacy(Addr legacy) {
    legacy_ = legacy;
    return *this;
  }

  // Return the number of function pointers present.
  constexpr size_t size() const { return array_.size() + (legacy_ ? 1 : 0); }

  constexpr bool empty() const { return size() == 0; }

  // Call init(Addr, bool) exactly size() times.  The flag in each callback is
  // true iff Addr has already been relocated.  The argument flag should be
  // true iff relocations affecting RELRO data have already been applied.
  template <typename T>
  constexpr void VisitInit(T&& init, bool relocated) {
    if (legacy_) {
      init(*legacy_, false);
    }
    for (const auto& addr : array_) {
      init(addr, relocated);
    }
  }

  // Same as VisitInit, but in the reverse order.
  template <typename T>
  constexpr void VisitFini(T&& fini, bool relocated) {
    for (auto it = array_.rbegin(); it != array_.rend(); ++it) {
      fini(*it, relocated);
    }
    if (legacy_) {
      fini(*legacy_, false);
    }
  }

  // This returns a callback suitable to pass to VisitInit or VisitFini to
  // directly call the functions right here.
  static constexpr auto RelocatedCall(size_type bias) {
    return [bias](const Addr& addr, bool relocated) {
      uintptr_t fnaddr = addr;
      if (!relocated) {
        fnaddr += bias;
      }
      auto fn = reinterpret_cast<InitFiniFunction*>(fnaddr);
      (*fn)();
    };
  }

  // Call all the functions in initialization order.
  void CallInit(size_type bias, bool relocated = true) {
    VisitInit(RelocatedCall(bias), relocated);
  }

  // Call all the functions in finalization order.
  void CallFini(size_type bias, bool relocated = true) {
    VisitFini(RelocatedCall(bias), relocated);
  }

 private:
  cpp20::span<const Addr> array_;
  std::optional<Addr> legacy_;
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INIT_FINI_H_
