// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_VOLUMES_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_VOLUMES_H_

#include <fidl/fuchsia.io/cpp/markers.h>
#include <fidl/fuchsia.io/cpp/wire.h>

#include "lib/zx/channel.h"

namespace fs_management {

// Adds volume |name| to the filesystem instance.  |crypt_client| is an optional channel to a Crypt
// service, in which case the volume will be encrypted.
//
// On success, |outgoing_dir| will be passed to the filesystem and bound to the volume's outgoing
// directory.  The channel will be closed on failure.
//
// Currently this is only supported for Fxfs.
__EXPORT zx::status<> CreateVolume(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                                   std::string_view name,
                                   fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
                                   zx::channel crypt_client = {});

// Opens volume |name| in the filesystem instance.  |crypt_client| is an optional channel to
// a Crypt service instance, in which case the volume is decrypted using that service.
//
// On success, |outgoing_dir| will be passed to the filesystem and bound to the volume's outgoing
// directory.  The channel will be closed on failure.
//
// Currently this is only supported for Fxfs.
__EXPORT zx::status<> OpenVolume(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                                 std::string_view name,
                                 fidl::ServerEnd<fuchsia_io::Directory> outgoing_dir,
                                 zx::channel crypt_client = {});

// Checks volume |name| in the filesystem instance.  |crypt_client| is an optional channel to
// a Crypt service instance, in which case the volume is decrypted using that service.
//
// Currently this is only supported for Fxfs.
__EXPORT zx::status<> CheckVolume(fidl::UnownedClientEnd<fuchsia_io::Directory> exposed_dir,
                                  std::string_view name, zx::channel crypt_client = {});

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_VOLUMES_H_
