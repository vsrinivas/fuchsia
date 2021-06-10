// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/string.h>
#include <zxtest/base/assertion.h>

namespace zxtest {

Assertion::Assertion(const fbl::String& desc, const fbl::String& expected,
                     const fbl::String& expected_eval, const fbl::String& actual,
                     const fbl::String& actual_eval, const SourceLocation& location, bool is_fatal)
    : message_(desc, location),
      expected_(expected),
      expected_eval_(expected_eval),
      actual_(actual),
      actual_eval_(actual_eval),
      is_fatal_(is_fatal),
      has_values_(true) {}

Assertion::Assertion(const fbl::String& desc, const SourceLocation& location, bool is_fatal)
    : message_(desc, location), is_fatal_(is_fatal), has_values_(false) {}

Assertion::Assertion(Assertion&& other) noexcept = default;
Assertion::~Assertion() = default;

}  // namespace zxtest
