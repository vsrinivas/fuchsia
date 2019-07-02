// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/frobinator_impl.h"

#include <lib/fit/function.h>

#include <utility>

#include "gtest/gtest.h"

namespace fidl {
namespace test {

FrobinatorImpl::FrobinatorImpl(fit::closure on_destroy) : on_destroy_(std::move(on_destroy)){};

FrobinatorImpl::~FrobinatorImpl() { on_destroy_(); };

void FrobinatorImpl::Frob(std::string value) { frobs.push_back(std::move(value)); }

void FrobinatorImpl::Grob(std::string value, GrobCallback callback) {
  grobs.push_back(std::move(value));
  callback("response");
}

}  // namespace test
}  // namespace fidl
