// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include "string_view.h"

namespace fidl {

class ErrorReporter {
public:
    void ReportError(StringView error);
    void PrintReports();

private:
    std::vector<std::string> errors_;
};

} // namespace fidl
