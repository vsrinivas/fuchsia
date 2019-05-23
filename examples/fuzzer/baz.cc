
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "baz.h"

#include <stdlib.h>

#include "src/lib/fxl/strings/split_string.h"

namespace examples {
namespace fuzzing {

int Baz::Execute(const std::string &commands) {
  auto lines = fxl::SplitStringCopy(commands, "\n", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  for (auto line : lines) {
    auto tokens = fxl::SplitStringCopy(line, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    auto len = tokens.size();
    if (tokens[0].compare("set") != 0) {
        continue;
    }
    if (len < 3) {
        return -1;
    }
    int bar = atoi(tokens[2].c_str());
    if (bar == 0) {
        return -1;
    }
    if (tokens[1].compare("foo") == 0) {
        std::unique_ptr<Foo> foo(new Foo(bar));
        SetFoo(std::move(foo));
    } else if (tokens[1].compare("foo") == 0) {
        SetBar(bar);
    } else {
        return -1;
    }
  }
  return 0;
}

void Baz::SetFoo(std::unique_ptr<Foo> foo) {
    foo_.reset(foo.get());
    if (bar_ == nullptr) {
        bar_ = &foo_->bar_;
    }
}

void Baz::SetBar(int bar) {
    if (bar_ != nullptr) {
        *bar_ = bar;
    }
}

} // namespace fuzzing
} // namespace examples

