// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <type_traits>

namespace ktl {

using std::is_const;
using std::is_const_v;

using std::is_lvalue_reference;
using std::is_lvalue_reference_v;

using std::is_pod;
using std::is_pod_v;

using std::is_same;
using std::is_same_v;

using std::remove_const;
using std::remove_const_t;

using std::remove_reference;
using std::remove_reference_t;

} // namespace ktl
