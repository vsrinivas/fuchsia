// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/memory/ref_ptr.h"

namespace escher {

// Convenient syntax for instantiating a new Reffable object.  For example,
// assuming that Foo inherits from Reffable or fxl::RefCountedThreadSafe:
//   fxl::RefPtr<Foo> foo = Make<Foo>(int_arg, "string_arg");
template <typename T, typename... Args>
fxl::RefPtr<T> Make(Args&&... args) {
  return fxl::internal::MakeRefCountedHelper<T>::MakeRefCounted(
      std::forward<Args>(args)...);
}

}  // namespace escher
