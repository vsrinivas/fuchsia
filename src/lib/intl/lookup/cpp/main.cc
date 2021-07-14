// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fpromise/result.h>

#include <iostream>
#include <vector>

#include "lookup.h"

using intl::Lookup;

int main() {
  const std::vector<std::string> locales = {"foo", "bar", "baz"};
  auto lookup_or = Lookup::New(locales);
  if (lookup_or.is_error()) {
    const bool code = lookup_or.error() == Lookup::Status::UNAVAILABLE;
    std::cout << "error: " << code << std::endl;
    exit(code);
  }
  auto lookup = lookup_or.take_value();
  const uint64_t magic = 100u;
  auto str_or = lookup->String(magic).take_value();
  std::cout << "translate: " << str_or;
  exit(0);
}
