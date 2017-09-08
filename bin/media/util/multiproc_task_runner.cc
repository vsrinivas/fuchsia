// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/util/multiproc_task_runner.h"

#include <magenta/syscalls/port.h>

#include "lib/ftl/logging.h"

namespace media {
namespace {
static const uint64_t kUpdateKey = 0;
static const uint64_t kQuitKey = 1;
}  // namespace

MultiprocTaskRunner::MultiprocTaskRunner(uint32_t thread_count) {
  FTL_DCHECK(thread_count > 0);

  mx_status_t status = mx::port::create(0u, &port_);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::port::create failed, status " << status;
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

void MultiprocTaskRunner::PostTask(ftl::Closure task) {
  ftl::Closure* task_copy = new ftl::Closure(task);
  QueuePacket(kUpdateKey, task_copy);
}

void MultiprocTaskRunner::PostTaskForTime(ftl::Closure task,
                                          ftl::TimePoint target_time) {
  FTL_CHECK(false) << "MultiprocTaskRunner::PostTaskForTime not implemented";
}

void MultiprocTaskRunner::PostDelayedTask(ftl::Closure task,
                                          ftl::TimeDelta delay) {
  FTL_CHECK(false) << "MultiprocTaskRunner::PostDelayedTask not implemented";
}

bool MultiprocTaskRunner::RunsTasksOnCurrentThread() {
  return false;
}

void MultiprocTaskRunner::Worker(uint32_t thread_number) {
  while (true) {
    mx_port_packet_t packet;
    mx_status_t status = port_.wait(MX_TIME_INFINITE, &packet, 0u);
    if (status != MX_OK) {
      FTL_LOG(ERROR) << "mx::port::wait failed, status " << status;
      break;
    }

    FTL_DCHECK(packet.type == MX_PKT_TYPE_USER);
    FTL_DCHECK(packet.key == kUpdateKey || packet.key == kQuitKey);

    if (packet.key == kQuitKey) {
      FTL_LOG(INFO) << "MultiprocTaskRunner::Worker#" << thread_number
                    << ": quitting";
      break;
    }

    ftl::Closure* task = reinterpret_cast<ftl::Closure*>(packet.user.u64[0]);
    FTL_DCHECK(task);
    (*task)();
    delete task;
  }
}

void MultiprocTaskRunner::QueuePacket(uint64_t key, void* payload) {
  mx_port_packet_t packet;
  packet.type = MX_PKT_TYPE_USER;
  packet.key = key;
  packet.user.u64[0] = reinterpret_cast<uint64_t>(payload);
  mx_status_t status = port_.queue(&packet, 0u);
  if (status != MX_OK) {
    FTL_LOG(ERROR) << "mx::port::queue failed, status " << status;
  }
}

}  // namespace media
