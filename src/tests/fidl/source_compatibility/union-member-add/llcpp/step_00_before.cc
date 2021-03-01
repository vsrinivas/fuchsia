// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <optional>
#include <string>

#include <fidl/test/unionmemberadd/llcpp/fidl.h>  // nogncheck
namespace fidl_test = llcpp::fidl::test::unionmemberadd;

std::optional<int32_t> parse_as_int(const std::string& s) {
  char* end;
  long int n = strtol(s.c_str(), &end, 10);
  if (end)
    return static_cast<int32_t>(n);
  return {};
}

// [START contents]
fidl_test::wire::JsonValue writer(const std::string& s) {
  std::optional<int32_t> maybe_int = parse_as_int(s);
  if (maybe_int) {
    return fidl_test::wire::JsonValue::WithIntValue(std::make_unique<int32_t>(*maybe_int));
  }
  return fidl_test::wire::JsonValue::WithStringValue(
      std::make_unique<fidl::StringView>(fidl::heap_copy_str(s)));
}

std::string reader(const fidl_test::wire::JsonValue& value) {
  switch (value.which()) {
    case fidl_test::wire::JsonValue::Tag::kIntValue:
      return std::to_string(value.int_value());
    case fidl_test::wire::JsonValue::Tag::kStringValue:
      return std::string(value.string_value().data(), value.string_value().size());
    case fidl_test::wire::JsonValue::Tag::kUnknown:
      return "<unknown>";
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
