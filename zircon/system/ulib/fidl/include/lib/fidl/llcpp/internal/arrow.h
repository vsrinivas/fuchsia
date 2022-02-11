// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_ARROW_H_
#define LIB_FIDL_LLCPP_INTERNAL_ARROW_H_

#include <utility>

namespace fidl {
namespace internal {

// A wrapper object which delegates calls to |Impl| using the "->" operator.
//
// It is useful for building modular messaging APIs with two separate naming
// spaces: the FIDL methods are exposed behind "->", while other utility methods
// are exposed behind the "." operator:
//
//     class MyClient {
//      public:
//       auto operator->() { return Arrow<SomeClientImpl>(this); }
//       void Bar() { ... }
//     };
//
//     MyClient client;
//     client->Foo();  // |Foo| is in |SomeClientImpl|.
//     client.Bar();   // |Bar| is in |MyClient|.
//
// The methods from |SomeClientImpl| are accessible behind "->".
template <typename Impl>
struct Arrow {
 public:
  template <typename... Args>
  explicit Arrow(Args&&... args) : impl_(std::forward<Args>(args)...) {}

  // Copying/moving around this object is dangerous as it may lead to dangling
  // references. Disable these operations for now.
  Arrow(const Arrow&) = delete;
  Arrow& operator=(const Arrow&) = delete;
  Arrow(Arrow&&) = delete;
  Arrow& operator=(Arrow&&) = delete;

  // Returns a pointer to the concrete messaging implementation.
  Impl* operator->() { return &impl_; }

 private:
  Impl impl_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_ARROW_H_
