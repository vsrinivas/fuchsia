// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/unionstrictflexible/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::unionstrictflexible;

// [START contents]
void use_union(fidl_test::JsonValue value) {
  switch (value.Which()) {
    case fidl_test::JsonValue::Tag::kIntValue:
      printf("int value: %d\n", value.int_value());
      break;
    case fidl_test::JsonValue::Tag::kStringValue:
      printf("string value: %s\n", value.string_value().c_str());
      break;
    case fidl_test::JsonValue::Tag::Invalid:
      printf("<uninitialized union>\n");
      break;
  }
}

// [END contents]

int main(int argc, const char** argv) { return 0; }
