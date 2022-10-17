// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_FS_TEST_CRYPT_SERVICE_H_
#define SRC_STORAGE_FS_TEST_CRYPT_SERVICE_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fidl/cpp/wire/client.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>

namespace fs_test {

// Configures a crypt service with random keys.  service_directory should be the
// service directory where the CryptManagement protocol can be found.
zx::result<> SetUpCryptWithRandomKeys(
    fidl::UnownedClientEnd<fuchsia_io::Directory> service_directory);

// Returns a handle to a crypt service configured with random keys.  The first call requires some
// one-time setup and is not thread-safe.  To use this, the fxfs crypt service must be included in
// the package and an appropriate shard must be included in the component that wants to use this.
// See existing use for examples.
zx::result<zx::channel> GetCryptService();

}  // namespace fs_test

#endif  // SRC_STORAGE_FS_TEST_CRYPT_SERVICE_H_
