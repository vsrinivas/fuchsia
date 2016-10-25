// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace mx {

template <typename T> struct handle_traits {
    static const bool supports_duplication = true;
};

} // namespace mx
