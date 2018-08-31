// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_

#include <string>
#include <vector>

namespace fidl {

class ErrorReporter {
public:
    void ReportError(std::string error);
    void PrintReports();

private:
    std::vector<std::string> errors_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
