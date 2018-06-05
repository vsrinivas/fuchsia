// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/public/lib/fxl/strings/string_view.h"

namespace zxdb {

// Returns true if the first argument ends in exactly the second.
bool StringEndsWith(fxl::StringView str, fxl::StringView ends_with);

}  // namespace zxdb
