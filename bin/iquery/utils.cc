// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fostr/hex_dump.h>
#include <lib/fxl/files/path.h>
#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/strings/string_printf.h>
#include <lib/fxl/strings/utf_codecs.h>

#include "garnet/bin/iquery/options.h"
#include "garnet/bin/iquery/utils.h"

#include <iostream>

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

}  // namespace

ObjectNode::ObjectNode() = default;
ObjectNode::ObjectNode(std::string name) { object.name = std::move(name); }

ObjectNode::ObjectNode(fuchsia::inspect::Object object)
    : object(std::move(object)) {}

ObjectNode::ObjectNode(ObjectNode&&) = default;
ObjectNode& ObjectNode::operator=(ObjectNode&&) = default;

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

std::string FormatString(fidl::StringPtr val) {
  if (fxl::IsStringUTF8(*val)) {
    return val->data();
  } else {
    return fxl::StringPrintf("Binary: %s", HexDump(*val).c_str());
  }
}

std::string FormatMetricValue(const fuchsia::inspect::Metric& metric) {
  std::string out;
  if (metric.value.is_int_value()) {
    out = fxl::StringPrintf("%ld", metric.value.int_value());
  } else if (metric.value.is_uint_value()) {
    out = fxl::StringPrintf("%lu", metric.value.uint_value());
  } else if (metric.value.is_double_value()) {
    out = fxl::StringPrintf("%f", metric.value.double_value());
  } else {
    // We already know which object we're outputting at this point.
    FXL_LOG(WARNING) << "Unknown metric type";
  }
  return out;
}

}  // namespace iquery
