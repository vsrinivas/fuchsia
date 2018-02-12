// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// This file provides some typedefs for a couple of common containers so code
// can be shared between host and target. See system/ulib/debug/README.md.

#ifdef __Fuchsia__
#include "fbl/string.h"
#include "fbl/unique_ptr.h"
#include "fbl/vector.h"
#else
#include <memory>
#include <string>
#include <vector>
#endif

namespace debug_ipc {
namespace shared {

#ifdef __Fuchsia__  // Userspace target library using FBL

using string = fbl::String;

template<typename T>
using unique_ptr = fbl::unique_ptr<T>;

template<typename T>
using vector = fbl::Vector<T>;

#else  // Host library using libc++.

using string = std::string;

template<typename T>
using unique_ptr = std::unique_ptr<T>;

template<typename T>
using vector = std::vector<T>;

#endif

}  // namespace shared
}  // namespace debug_ipc
