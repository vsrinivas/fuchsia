// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TRAITS_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TRAITS_H_

#include <memory>

namespace fidl::flat {

// This file defines abstract base classes for common traits/behaviors in the
// flat AST. These exist mostly for the sake of documentation. They use CRTP
// (https://en.wikipedia.org/wiki/Curiously_recurring_template_pattern) to refer
// to the type of `this` in method signatures.

// Mixin that disables the copy constructor, useful for traits that provide
// other ways of copying objects.
struct NoCopy {
  NoCopy() = default;
  NoCopy(NoCopy&&) = default;
  NoCopy(const NoCopy&) = delete;
};

// A type that supports polymorphic cloning. This must clone the entire object,
// including any compilation state that was set after construction.
template <typename T>
struct HasClone : private NoCopy {
  virtual std::unique_ptr<T> Clone() const = 0;
};

// A type that supports copying. Unlike `Clone`, `Copy` returns the object by
// value (and so it is not used polymorphically). We could just use copy
// constructors, but that makes it too easy to copy objects by accident (e.g.
// using `auto` instead of `auto&`), whereas `.Copy()` is explicit.
template <typename T>
struct HasCopy : private NoCopy {
  virtual T Copy() const = 0;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_TRAITS_H_
