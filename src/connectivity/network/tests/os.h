// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTS_OS_H_
#define SRC_CONNECTIVITY_NETWORK_TESTS_OS_H_

constexpr bool kIsFuchsia =
#if defined(__Fuchsia__)
    true
#elif defined(__linux__)
    false
#else
#error("unsupported OS")
#endif
    ;

#endif  // SRC_CONNECTIVITY_NETWORK_TESTS_OS_H_
