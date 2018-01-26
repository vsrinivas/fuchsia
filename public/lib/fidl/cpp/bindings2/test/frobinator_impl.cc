// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/test/frobinator_impl.h"

#include <utility>

#include "gtest/gtest.h"

namespace fidl {
namespace test {

FrobinatorImpl::FrobinatorImpl() = default;

FrobinatorImpl::~FrobinatorImpl() = default;

void FrobinatorImpl::Frob(StringPtr value) {
  EXPECT_FALSE(value.is_null());
  frobs.push_back(std::move(*value));
}

void FrobinatorImpl::Grob(StringPtr value, GrobCallback callback) {
  EXPECT_FALSE(value.is_null());
  grobs.push_back(std::move(*value));
  callback("response");
}

}  // namespace test
}  // namespace fidl
