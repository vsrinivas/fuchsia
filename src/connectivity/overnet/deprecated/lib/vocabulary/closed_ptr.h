// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

namespace overnet {

namespace closed_ptr_details {

template <class T>
class Closer {
 public:
  void operator()(T* p) {
    p->Close([p]() { delete p; });
  }
};

}  // namespace closed_ptr_details

template <class T, class Base = T>
using ClosedPtr = std::unique_ptr<T, closed_ptr_details::Closer<Base>>;

template <class T, class Base = T, class... Args>
ClosedPtr<T, Base> MakeClosedPtr(Args&&... args) {
  return ClosedPtr<T, Base>(new T(std::forward<Args>(args)...));
}

}  // namespace overnet
