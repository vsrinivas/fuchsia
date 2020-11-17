// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/frobinator_impl.h"

#include <lib/fit/function.h>

#include <utility>

#include <zxtest/zxtest.h>

#include "lib/fit/result.h"

namespace fidl {
namespace test {

FrobinatorImpl::FrobinatorImpl(fit::closure on_destroy) : on_destroy_(std::move(on_destroy)){};

FrobinatorImpl::~FrobinatorImpl() { on_destroy_(); };

void FrobinatorImpl::Frob(std::string value) { frobs.push_back(std::move(value)); }

void FrobinatorImpl::Grob(std::string value, GrobCallback callback) {
  grobs.push_back(std::move(value));
  callback("response");
}

void FrobinatorImpl::Fail(bool fail, FailCallback callback) {
  if (fail) {
    callback(fit::error(42U));
  } else {
    callback(fit::ok());
  }
}

void FrobinatorImpl::FailHard(bool fail, FailHardCallback callback) {
  if (fail) {
    callback(fit::error(42U));
  } else {
    callback(fit::ok(std::string("hello, world")));
  }
}

void FrobinatorImpl::FailHardest(bool fail, FailHardestCallback callback) {
  if (fail) {
    callback(fit::error(42U));
  } else {
    callback(fit::ok(std::make_tuple(std::string("hello"), std::string("world"))));
  }
}

}  // namespace test
}  // namespace fidl
