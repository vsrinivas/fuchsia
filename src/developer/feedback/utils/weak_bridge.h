// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_UTILS_WEAK_BRIDGE_H_
#define SRC_DEVELOPER_FEEDBACK_UTILS_WEAK_BRIDGE_H_

#include <lib/fit/bridge.h>

#include "src/lib/fxl/memory/weak_ptr.h"

namespace feedback {

template <typename V = void, typename E = void>
class WeakBridge {
 public:
  WeakBridge() : weak_ptr_factory_(&bridge_) {}
  fxl::WeakPtr<fit::bridge<V, E>> GetWeakPtr() { return weak_ptr_factory_.GetWeakPtr(); }

 private:
  fit::bridge<V, E> bridge_;
  fxl::WeakPtrFactory<fit::bridge<V, E>> weak_ptr_factory_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_UTILS_WEAK_BRIDGE_H_
