// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <utility>

namespace fidl {

template <typename T, typename... Arguments>
std::unique_ptr<T> make_unique(Arguments... arguments) {
    return std::unique_ptr<T>(new T(std::forward<Arguments>(arguments)...));
}

} // namespace fidl
