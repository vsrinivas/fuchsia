// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FSHOST_ADMIN_CLIENT_H_
#define SRC_STORAGE_FSHOST_ADMIN_CLIENT_H_

#include <fidl/fuchsia.fshost/cpp/markers.h>
#include <lib/zx/result.h>

namespace fshost {

zx::result<fidl::ClientEnd<fuchsia_fshost::Admin>> ConnectToAdmin();

}  // namespace fshost

#endif  // SRC_STORAGE_FSHOST_ADMIN_CLIENT_H_
