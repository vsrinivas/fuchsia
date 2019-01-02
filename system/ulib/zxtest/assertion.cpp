// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/base/assertion.h>

namespace zxtest {

Assertion::Assertion(const fbl::String& expected, const fbl::String& expected_eval,
                     const fbl::String& actual, const fbl::String& actual_eval,
                     const SourceLocation& location, bool is_fatal)
    : expected_(expected), expected_eval_(expected_eval), actual_(actual),
      actual_eval_(actual_eval), location_(location), is_fatal_(is_fatal) {}

Assertion::Assertion(Assertion&& other) = default;
Assertion::~Assertion() = default;

} // namespace zxtest
