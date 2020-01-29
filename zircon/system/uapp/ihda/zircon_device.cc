// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon_device.h"

#include <dirent.h>
#include <fcntl.h>
#include <fuchsia/hardware/intel/hda/c/fidl.h>
#include <lib/fdio/io.h>
#include <lib/fdio/cpp/caller.h>
#include <stdio.h>
#include <zircon/device/intel-hda.h>

#include <limits>

#include <fbl/unique_fd.h>

namespace audio {
namespace intel_hda {

uint32_t ZirconDevice::transaction_id_ = 0;

zx_status_t ZirconDevice::Connect() {
  if (dev_channel_ != ZX_HANDLE_INVALID)
    return ZX_OK;

  if (!dev_name_)
    return ZX_ERR_NO_MEMORY;

  fbl::unique_fd fd{::open(dev_name_, O_RDONLY)};
  if (!fd.is_valid())
    return ZX_ERR_NOT_FOUND;

  fdio_cpp::FdioCaller dev(std::move(fd));
  zx_status_t (*thunk)(zx_handle_t, zx_handle_t*);

  switch (type_) {
    case Type::Controller:
      thunk = fuchsia_hardware_intel_hda_ControllerDeviceGetChannel;
      break;

    case Type::Codec:
      thunk = fuchsia_hardware_intel_hda_CodecDeviceGetChannel;
      break;

    default:
      return ZX_ERR_INTERNAL;
  }

  zx_status_t res = thunk(dev.borrow_channel(), dev_channel_.reset_and_get_address());
  if (res != ZX_OK) {
    printf("[%s] Failed to fetch device channel (%d)\n", dev_name(), res);
  }

  return res;
}

void ZirconDevice::Disconnect() { dev_channel_.reset(); }

zx_status_t ZirconDevice::CallDevice(const zx_channel_call_args_t& args, zx::duration timeout) {
  uint32_t resp_size;
  uint32_t resp_handles;
  zx::time deadline =
      timeout == zx::duration::infinite() ? zx::time::infinite() : zx::deadline_after(timeout);

  return dev_channel_.call(0, deadline, &args, &resp_size, &resp_handles);
}

zx_status_t ZirconDevice::Enumerate(void* ctx, const char* const dev_path, EnumerateCbk cbk) {
  static constexpr size_t FILENAME_SIZE = 256;

  struct dirent* de;
  DIR* dir = opendir(dev_path);
  zx_status_t res = ZX_OK;
  char buf[FILENAME_SIZE];

  if (!dir)
    return ZX_ERR_NOT_FOUND;

  while ((de = readdir(dir)) != NULL) {
    uint32_t id;
    if (sscanf(de->d_name, "%u", &id) == 1) {
      size_t total = 0;

      total += snprintf(buf + total, sizeof(buf) - total, "%s/", dev_path);
      total += snprintf(buf + total, sizeof(buf) - total, "%03u", id);

      res = cbk(ctx, id, buf);
      if (res != ZX_OK)
        goto done;
    }
  }

done:
  closedir(dir);
  return res;
}

}  // namespace intel_hda
}  // namespace audio
