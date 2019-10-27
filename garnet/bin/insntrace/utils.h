// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_INSNTRACE_UTILS_H_
#define GARNET_BIN_INSNTRACE_UTILS_H_

#include <zircon/types.h>

namespace insntrace {

// Log an error in a FIDL request.
// |fidl_status| is the status of the underlying FIDL protocol call. If it is not ZX_OK then
// it is printed and |rqst_status| is ignored.
// |rqst_status| is the status of |rqst_name|. If it is not ZX_OK then it is printed.
// If both |fidl_status,rqst_status| are ZX_OK then nothing is printed.
void LogFidlFailure(const char* rqst_name, zx_status_t fidl_status,
                    zx_status_t rqst_status = ZX_OK);

}  // namespace insntrace

#endif  // GARNET_BIN_INSNTRACE_UTILS_H_
