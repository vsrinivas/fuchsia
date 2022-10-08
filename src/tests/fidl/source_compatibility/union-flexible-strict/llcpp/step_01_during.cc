// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fidl.test.unionflexiblestrict/cpp/wire.h>  // nogncheck
namespace fidl_test = fidl_test_unionflexiblestrict;

// [START contents]
void use_union(fidl_test::wire::JsonValue* value) {
  switch (value->Which()) {
    case fidl_test::wire::JsonValue::Tag::kIntValue:
      printf("int value: %d\n", value->int_value());
      break;
    case fidl_test::wire::JsonValue::Tag::kStringValue:
      printf("string value: %s\n", value->string_value().data());
      break;
    default:
      printf("<unknown member: %" PRIu64 ">\n", static_cast<fidl_xunion_tag_t>(value->Which()));
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
