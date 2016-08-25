// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utils/tests/intrusive_containers/objects.h>

namespace utils {
namespace tests {
namespace intrusive_containers {

size_t TestObjBase::live_obj_count_ = 0;

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace utils
