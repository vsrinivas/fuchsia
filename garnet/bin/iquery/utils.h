// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_UTILS_H_
#define GARNET_BIN_IQUERY_UTILS_H_

#include <string>

#include <fuchsia/inspect/cpp/fidl.h>
#include <src/lib/fxl/macros.h>

#include "garnet/bin/iquery/options.h"

// Utility functions that are transversal to modes.

namespace iquery {

// If option is none, will return the provided name,
// full_paths return the given path and absolute will create the absolute path.
std::string FormatPath(Options::PathFormatting, const std::string& path,
                       const std::string& name = "");

// Format a string handling the case where the string is not a valid UTF8
// string by outputting a hex dump.
std::string FormatStringHexFallback(fxl::StringView val);

// Format a string handling the case where the string is not a valid UTF8
// string by outputting the string encoded in Base64.
std::string FormatStringBase64Fallback(fxl::StringView val);

// Metric values have a lot of representations (int, uint, etc.).
// This functions returns a string representing the correct value.
std::string FormatMetricValue(const fuchsia::inspect::Metric& metric);

bool IsStringPrintable(fxl::StringView input);

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_UTILS_H_
