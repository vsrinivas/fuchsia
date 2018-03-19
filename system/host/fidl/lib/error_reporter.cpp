// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/error_reporter.h"

namespace fidl {

void ErrorReporter::ReportError(std::string error) {
    errors_.push_back(std::move(error));
}

void ErrorReporter::PrintReports() {
    for (const auto& error : errors_) {
        fprintf(stderr, "%s", error.data());
    }
}

} // namespace fidl
