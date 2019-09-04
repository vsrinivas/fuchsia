// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/tests/crasher_wrapper.h"

#include <lib/fdio/spawn.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>

namespace fuchsia {
namespace exception {

bool SpawnCrasher(ProcessException* pe) {
  zx::unowned_job current_job(zx_job_default());
  if (zx_status_t res = zx::job::create(*current_job, 0, &pe->job); res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  if (zx_status_t res = pe->job.create_exception_channel(0, &pe->exception_channel); res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  if (zx_status_t res = zx::port::create(0, &pe->port); res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  constexpr uint64_t kKey = 0x1234;
  if (zx_status_t res = pe->exception_channel.wait_async(pe->port, kKey, ZX_CHANNEL_READABLE, 0);
      res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  // Create the process.
  const char* argv[] = {"crasher", nullptr};
  constexpr char kCrasherPath[] = "/pkg/bin/exception_broker_crasher";
  char err_msg[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  if (zx_status_t res =
          fdio_spawn_etc(pe->job.get(), FDIO_SPAWN_CLONE_ALL, kCrasherPath, argv, nullptr, 0,
                         nullptr, pe->process.reset_and_get_address(), err_msg);
      res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  // Wait for the exception.
  zx_port_packet_t packet;
  if (zx_status_t res = pe->port.wait(zx::time::infinite(), &packet); res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  if ((packet.key != kKey) || (packet.type != ZX_PKT_TYPE_SIGNAL_ONE) ||
      ((packet.signal.observed & ZX_CHANNEL_READABLE) == 0)) {
    FX_PLOGS(ERROR, ZX_ERR_WRONG_TYPE) << "Received wrong port packet.";
    return false;
  }

  // Read the exception.
  if (zx_status_t res =
          pe->exception_channel.read(0, &pe->exception_info, pe->exception.reset_and_get_address(),
                                     sizeof(pe->exception_info), 1, nullptr, nullptr);
      res != ZX_OK) {
    FX_PLOGS(ERROR, res);
    return false;
  }

  return true;
}

}  // namespace exception
}  // namespace fuchsia
