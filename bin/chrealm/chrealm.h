// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_CHREALM_CHREALM_H_
#define GARNET_BIN_CHREALM_CHREALM_H_

#include <string>

#include <zircon/status.h>

namespace chrealm {

zx_status_t RunBinaryInRealm(const std::string& realm_path, const char** argv,
                             int64_t* return_code);

}  // namespace chrealm

#endif  // GARNET_BIN_CHREALM_CHREALM_H_
