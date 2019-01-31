// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_ERROR_REPORTER_H_
#define ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_ERROR_REPORTER_H_

#include <string>
#include <vector>

#include "source_location.h"
#include "string_view.h"
#include "token.h"

namespace banjo {

class ErrorReporter {
public:
    void ReportError(const SourceLocation& location, StringView message);
    void ReportError(const Token& token, StringView message);
    void ReportError(StringView message);
    void PrintReports();
    const std::vector<std::string>& errors() const { return errors_; };

private:
    std::vector<std::string> errors_;
};

} // namespace banjo

#endif // ZIRCON_SYSTEM_HOST_BANJO_INCLUDE_BANJO_ERROR_REPORTER_H_
