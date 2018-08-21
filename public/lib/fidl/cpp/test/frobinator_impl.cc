// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/frobinator_impl.h"

#include <utility>

#include <lib/fit/function.h>
#include "gtest/gtest.h"

namespace fidl {
namespace test {

FrobinatorImpl::FrobinatorImpl(fit::closure on_destroy)
    : on_destroy_(std::move(on_destroy)){};

FrobinatorImpl::~FrobinatorImpl() { on_destroy_(); };

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
