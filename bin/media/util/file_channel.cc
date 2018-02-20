// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/util/file_channel.h"

#include <fdio/limits.h>
#include <fdio/util.h>
#include <zircon/processargs.h>

namespace media {

zx::channel ChannelFromFd(fxl::UniqueFD fd) {
  FXL_DCHECK(fd.is_valid());
  zx_handle_t handles[FDIO_MAX_HANDLES];
  uint32_t types[FDIO_MAX_HANDLES];

  zx_status_t status = fdio_transfer_fd(fd.release(), 0, handles, types);
  if (status != 1) {
    FXL_LOG(ERROR) << "fdio_transfer_fd returned " << status << ", expected 1";
    return zx::channel();
  }

  if (types[0] != PA_FDIO_REMOTE) {
    FXL_LOG(ERROR) << "fdio_transfer_fd return a handle of type 0x" << std::hex
                   << types[0] << ", expected 0x32 (PA_FDIO_REMOTE)";
    return zx::channel();
  }

  return zx::channel(handles[0]);
}

fxl::UniqueFD FdFromChannel(zx::channel file_channel) {
  zx_handle_t handle = file_channel.release();
  uint32_t type = PA_FDIO_REMOTE;
  int fd;

  zx_status_t status = fdio_create_fd(&handle, &type, 1, &fd);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fdio_create_fd failed, status " << status;
    return fxl::UniqueFD();
  }

  return fxl::UniqueFD(fd);
}

}  // namespace media
