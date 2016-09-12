// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace mxtl {

template<class T>
constexpr const T& min(const T& a, const T& b) {
    return (b < a) ? b : a;
}

template<class T>
constexpr const T& max(const T& a, const T& b) {
    return (a < b) ? b : a;
}

template<class T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

}  // namespace mxtl
