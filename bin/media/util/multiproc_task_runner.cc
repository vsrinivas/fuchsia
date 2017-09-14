// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/util/multiproc_task_runner.h"

#include <zircon/syscalls/port.h>

#include "lib/fxl/logging.h"

namespace media {
namespace {
static const uint64_t kUpdateKey = 0;
static const uint64_t kQuitKey = 1;
}  // namespace

MultiprocTaskRunner::MultiprocTaskRunner(uint32_t thread_count) {
  FXL_DCHECK(thread_count > 0);

  zx_status_t status = zx::port::create(0u, &port_);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::port::create failed, status " << status;
  }

  while (thread_count-- != 0) {
    threads_.push_back(std::thread(
        [ this, thread_number = thread_count ]() { Worker(thread_number); }));
  }
}

MultiprocTaskRunner::~MultiprocTaskRunner() {
  for (size_t i = 0; i < threads_.size(); ++i) {
    QueuePacket(kQuitKey);
  }

  for (auto& thread : threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

void MultiprocTaskRunner::PostTask(fxl::Closure task) {
  fxl::Closure* task_copy = new fxl::Closure(task);
  QueuePacket(kUpdateKey, task_copy);
}

void MultiprocTaskRunner::PostTaskForTime(fxl::Closure task,
                                          fxl::TimePoint target_time) {
  FXL_CHECK(false) << "MultiprocTaskRunner::PostTaskForTime not implemented";
}

void MultiprocTaskRunner::PostDelayedTask(fxl::Closure task,
                                          fxl::TimeDelta delay) {
  FXL_CHECK(false) << "MultiprocTaskRunner::PostDelayedTask not implemented";
}

bool MultiprocTaskRunner::RunsTasksOnCurrentThread() {
  return false;
}

void MultiprocTaskRunner::Worker(uint32_t thread_number) {
  while (true) {
    zx_port_packet_t packet;
    zx_status_t status = port_.wait(ZX_TIME_INFINITE, &packet, 0u);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::port::wait failed, status " << status;
      break;
    }

    FXL_DCHECK(packet.type == ZX_PKT_TYPE_USER);
    FXL_DCHECK(packet.key == kUpdateKey || packet.key == kQuitKey);

    if (packet.key == kQuitKey) {
      FXL_LOG(INFO) << "MultiprocTaskRunner::Worker#" << thread_number
                    << ": quitting";
      break;
    }

    fxl::Closure* task = reinterpret_cast<fxl::Closure*>(packet.user.u64[0]);
    FXL_DCHECK(task);
    (*task)();
    delete task;
  }
}

void MultiprocTaskRunner::QueuePacket(uint64_t key, void* payload) {
  zx_port_packet_t packet;
  packet.type = ZX_PKT_TYPE_USER;
  packet.key = key;
  packet.user.u64[0] = reinterpret_cast<uint64_t>(payload);
  zx_status_t status = port_.queue(&packet, 0u);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::port::queue failed, status " << status;
  }
}

}  // namespace media
