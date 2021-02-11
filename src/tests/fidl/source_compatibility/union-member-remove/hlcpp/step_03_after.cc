// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <optional>
#include <sstream>

#include <fidl/test/unionmemberremove/cpp/fidl.h>  // nogncheck
namespace fidl_test = fidl::test::unionmemberremove;

std::optional<int32_t> parse_as_int(const std::string& s) {
  char* end;
  long int n = strtol(s.c_str(), &end, 10);
  if (end)
    return static_cast<int32_t>(n);
  return {};
}

// [START contents]
fidl_test::JsonValue writer(const std::string& s) {
  std::optional<int32_t> maybe_int = parse_as_int(s);
  if (maybe_int) {
    return fidl_test::JsonValue::WithIntValue(std::move(*maybe_int));
  }
  auto val = s;
  return fidl_test::JsonValue::WithStringValue(std::move(val));
}

std::string reader(const fidl_test::JsonValue& value) {
  switch (value.Which()) {
    case fidl_test::JsonValue::Tag::kIntValue:
      return std::to_string(value.int_value());
    case fidl_test::JsonValue::Tag::kStringValue:
      return value.string_value();
    case fidl_test::JsonValue::Tag::Invalid:
      return "<uninitialized>";
    case fidl_test::JsonValue::Tag::kUnknown: {
      std::ostringstream out;
      out << "<" << value.UnknownBytes()->size() << " unknown bytes>";
      return out.str();
    }
  }
}
// [END contents]

int main(int argc, const char** argv) { return 0; }
