// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_VOLUME_IMAGE_ADAPTER_COMMANDS_FILE_CLIENT_H_
#define SRC_STORAGE_VOLUME_IMAGE_ADAPTER_COMMANDS_FILE_CLIENT_H_

#include <fidl/fuchsia.io/cpp/wire.h>

// TODO(https://fxbug.dev/115641): Remove this when a more robust alternative exists.
zx::result<fidl::ClientEnd<fuchsia_io::File>> OpenFile(const char* path);

#endif  // SRC_STORAGE_VOLUME_IMAGE_ADAPTER_COMMANDS_FILE_CLIENT_H_
