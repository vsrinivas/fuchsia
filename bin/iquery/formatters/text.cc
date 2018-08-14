// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "text.h"

#include <sstream>

#include <lib/fostr/hex_dump.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/join_strings.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/strings/utf_codecs.h>

namespace iquery {

namespace {
constexpr size_t kMaxHexSize = 256;

std::string HexDump(const std::string& contents) {
  std::ostringstream out;
  if (contents.size() > kMaxHexSize) {
    out << "\nFirst " << kMaxHexSize << " bytes of " << contents.size();
  }
  out << fostr::HexDump(&contents.front(),
                        std::min(kMaxHexSize, contents.size()), 0x0);
  return out.str();
}

void AppendKey(std::string* out, ::fidl::StringPtr key) {
  if (fxl::IsStringUTF8(*key)) {
    fxl::StringAppendf(out, "  %s = ", key->data());
  } else {
    fxl::StringAppendf(out, "  Binary Key:%s\n  = ", HexDump(*key).c_str());
  }
}

}  // namespace

std::string TextFormatter::FormatFind(
    const std::vector<std::string>& find_results) {
  return fxl::Concatenate({fxl::JoinStrings(find_results, "\n"), "\n"});
}

std::string TextFormatter::FormatLs(
    const std::vector<fuchsia::inspect::Object>& ls_results) {
  std::string output;
  for (const auto& obj : ls_results) {
    fxl::StringAppendf(&output, "%s\n", obj.name->data());
  }
  return output;
}

std::string TextFormatter::FormatCat(
    const std::vector<fuchsia::inspect::Object>& objects) {
  std::string output;
  for (const auto& obj : objects) {
    fxl::StringAppendf(&output, "%s:\n", obj.name->data());
    for (const auto& prop : *obj.properties) {
      AppendKey(&output, prop.key);
      if (fxl::IsStringUTF8(*prop.value)) {
        fxl::StringAppendf(&output, "%s\n", prop.value->data());
      } else {
        fxl::StringAppendf(&output, "Binary Value:%s\n",
                           HexDump(*prop.value).c_str());
      }
    }
    for (const auto& metric : *obj.metrics) {
      AppendKey(&output, metric.key);
      if (metric.value.is_int_value()) {
        fxl::StringAppendf(&output, "%ld\n", metric.value.int_value());
      } else if (metric.value.is_uint_value()) {
        fxl::StringAppendf(&output, "%lu\n", metric.value.uint_value());
      } else if (metric.value.is_double_value()) {
        fxl::StringAppendf(&output, "%f\n", metric.value.double_value());
      } else {
        FXL_LOG(WARNING) << "Unknown metric type for " << obj.name;
      }
    }
  }

  return output;
}

}  // namespace iquery
