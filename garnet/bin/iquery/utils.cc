// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/iquery/utils.h"

#include <lib/fostr/hex_dump.h>
#include <src/lib/fxl/strings/concatenate.h>
#include <src/lib/fxl/strings/string_printf.h>
#include <src/lib/fxl/strings/utf_codecs.h>
#include <third_party/cobalt/util/crypto_util/base64.h>

#include <iostream>

#include "garnet/bin/iquery/options.h"
#include "lib/inspect/hierarchy.h"
#include "src/lib/files/path.h"

using cobalt::crypto::Base64Encode;
using inspect::hierarchy::DoubleMetric;
using inspect::hierarchy::IntMetric;
using inspect::hierarchy::Metric;
using inspect::hierarchy::UIntMetric;

namespace iquery {

namespace {

constexpr size_t kMaxHexSize = 256;
std::string HexDump(fxl::StringView contents) {
  std::ostringstream out;
  if (contents.size() > kMaxHexSize) {
    out << "\nFirst " << kMaxHexSize << " bytes of " << contents.size();
  }
  out << fostr::HexDump(contents.data(), std::min(kMaxHexSize, contents.size()),
                        0x0);
  return out.str();
}

}  // namespace

// TODO(donosoc): Cleanup artifacts like "//" or ending in '/'
std::string FormatPath(Options::PathFormatting path_format,
                       const std::string& path, const std::string& name) {
  switch (path_format) {
    case Options::PathFormatting::NONE:
      return name;
    case Options::PathFormatting::FULL:
      return path;
    case Options::PathFormatting::ABSOLUTE:
      return files::AbsolutePath(path);
  }
};

std::string FormatStringHexFallback(fxl::StringView val) {
  if (IsStringPrintable(val)) {
    return std::string(val.begin(), val.end());
  } else {
    return fxl::StringPrintf("Binary: %s", HexDump(val).c_str());
  }
}

std::string FormatStringBase64Fallback(fxl::StringView val) {
  if (IsStringPrintable(val)) {
    return std::string(val.begin(), val.end());
  } else {
    std::string content;
    Base64Encode((uint8_t*)val.data(), val.size(), &content);
    return fxl::Concatenate({"b64:", content});
  }
}

std::string FormatMetricValue(const Metric& metric) {
  switch (metric.format()) {
    case inspect::hierarchy::MetricFormat::INT:
      return fxl::StringPrintf("%ld", metric.Get<IntMetric>().value());
    case inspect::hierarchy::MetricFormat::UINT:
      return fxl::StringPrintf("%lu", metric.Get<UIntMetric>().value());
    case inspect::hierarchy::MetricFormat::DOUBLE:
      return fxl::StringPrintf("%f", metric.Get<DoubleMetric>().value());
    default:
      FXL_LOG(WARNING) << "Unknown metric type";
      return "";
  }
}

bool IsStringPrintable(fxl::StringView input) {
  if (!fxl::IsStringUTF8(input)) {
    return false;
  }

  // Ensure the string does not contain unprintable ASCII characters.
  uint32_t code_point;
  for (size_t index = 0; fxl::ReadUnicodeCharacter(input.data(), input.size(),
                                                   &index, &code_point) &&
                         index != input.size();
       index++) {
    // Skip any non-ASCII code points.
    if (code_point & (~0x7F)) {
      continue;
    }
    if (isprint(code_point) || code_point == '\t' || code_point == '\n' ||
        code_point == '\r') {
      continue;
    }
    return false;
  }
  return true;
}

}  // namespace iquery
