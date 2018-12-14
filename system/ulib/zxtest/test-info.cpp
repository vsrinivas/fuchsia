// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <zxtest/base/test-info.h>
#include <zxtest/base/test.h>
#include <zxtest/base/types.h>

namespace zxtest {
namespace internal {

TestInfo::TestInfo(const fbl::String& name, const SourceLocation& location, TestFactory factory)
    : factory_(std::move(factory)), name_(name), location_(location) {}
TestInfo::TestInfo(TestInfo&& rhs) = default;
TestInfo::~TestInfo() = default;

std::unique_ptr<Test> TestInfo::Instantiate(TestDriver* driver) const {
    return factory_(driver);
}

} // namespace internal
} // namespace zxtest
