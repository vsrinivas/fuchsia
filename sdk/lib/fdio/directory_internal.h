// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_DIRECTORY_INTERNAL_H_
#define LIB_FDIO_DIRECTORY_INTERNAL_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/zx/result.h>

namespace fdio_internal {

zx_status_t fdio_open_at(fidl::UnownedClientEnd<fuchsia_io::Directory> directory,
                         std::string_view path, fuchsia_io::wire::OpenFlags flags,
                         fidl::ServerEnd<fuchsia_io::Node> request);

}  // namespace fdio_internal

#endif  // LIB_FDIO_DIRECTORY_INTERNAL_H_
