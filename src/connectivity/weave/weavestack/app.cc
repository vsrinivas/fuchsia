// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/connectivity/weave/weavestack/app.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/logger.h>
#include <poll.h>
#include <zircon/types.h>

#include <Weave/DeviceLayer/PlatformManager.h>
namespace weavestack {
namespace {
using nl::Weave::DeviceLayer::PlatformMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
}  // namespace

constexpr struct timeval MAX_SELECT_SLEEP_TIME = {.tv_sec = 10};

App::App() = default;

App::~App() { Quit(); }

void App::Quit() {
  loop_.Quit();
  loop_.JoinThreads();
  ClearWaiters();
}

zx_status_t App::Init() {
  syslog::InitLogger({"weavestack"});

  if (initialized_) {
    return ZX_ERR_BAD_STATE;
  }

  WEAVE_ERROR err = PlatformMgr().InitWeaveStack();
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "InitWeaveStack() failed: " << nl::ErrorStr(err);
    return ZX_ERR_INTERNAL;
  }

  sleep_task_ = std::make_unique<async::TaskClosure>([this]{
    FdHandler(ZX_OK, 0);
  });
  initialized_ = true;

  return ZX_OK;
}

zx_status_t App::WaitForFd(int fd, uint32_t events) {
  auto waiter = std::make_unique<fsl::FDWaiter>(loop_.dispatcher());
  bool waited = waiter->Wait([this](zx_status_t status, uint32_t zero) { FdHandler(status, zero); },
                             fd, events);
  if (!waited) {
    FX_LOGS(ERROR) << "failed to wait for events on fd = " << fd;
  }
  waiters_.push_back(std::move(waiter));

  return ZX_OK;
}

// TODO(fxb/47096): tracks the integration test.
zx_status_t App::StartFdWaiters(void) {
  ClearWaiters();
  struct timeval sleep_time;
  memcpy(&sleep_time, &MAX_SELECT_SLEEP_TIME, sizeof(sleep_time));
  PlatformMgrImpl().GetSystemLayer().PrepareSelect(fds_.num_fds, &fds_.read_fds, &fds_.write_fds,
                                                   &fds_.except_fds, sleep_time);
  PlatformMgrImpl().GetInetLayer().PrepareSelect(fds_.num_fds, &fds_.read_fds, &fds_.write_fds,
                                                 &fds_.except_fds, sleep_time);

  for (auto fd = 0; fd < fds_.num_fds; ++fd) {
    uint32_t events = 0;
    if (FD_ISSET(fd, &fds_.read_fds)) {
      events |= POLLIN;
    }
    if (FD_ISSET(fd, &fds_.write_fds)) {
      events |= POLLOUT;
    }
    if (FD_ISSET(fd, &fds_.except_fds)) {
      events |= POLLERR;
    }
    if (events == 0) {
      continue;
    }
    zx_status_t status = WaitForFd(fd, events);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "error waiting for fd " << fd << ": " << status;
      return status;
    }
  }

  zx::duration duration( ZX_SEC(sleep_time.tv_sec) + ZX_USEC(sleep_time.tv_usec) );
  return sleep_task_->PostDelayed(loop_.dispatcher(), duration);
}
void App::ClearWaiters() {
  waiters_.clear();
  FD_ZERO(&fds_.read_fds);
  FD_ZERO(&fds_.write_fds);
  FD_ZERO(&fds_.except_fds);
  fds_.num_fds = 0;
  if (sleep_task_ != nullptr) {
    sleep_task_->Cancel();
  }
}

void App::FdHandler(zx_status_t status, uint32_t zero) {
  if (status == ZX_ERR_CANCELED) {
    FX_VLOGS(1) << "waiter cancelled, doing nothing";
    return;
  }

  struct timeval sleep_time;
  memset(&sleep_time, 0, sizeof(sleep_time));
  int res = select(fds_.num_fds, &fds_.read_fds, &fds_.write_fds, &fds_.except_fds, &sleep_time);
  if (res < 0) {
    FX_LOGS(ERROR) << "failed to select on fds: " << strerror(errno);
    loop_.Shutdown();
    return;
  }

  PlatformMgrImpl().GetSystemLayer().HandleSelectResult(res, &fds_.read_fds, &fds_.write_fds,
                                                        &fds_.except_fds);
  PlatformMgrImpl().GetInetLayer().HandleSelectResult(res, &fds_.read_fds, &fds_.write_fds,
                                                      &fds_.except_fds);
  // Wait for the next set of events.
  status = StartFdWaiters();
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "failed to wait for next packet: " << status;
    loop_.Shutdown();
  }
}

zx_status_t App::Run(zx::time deadline, bool once) {
  zx_status_t status = async::PostTask(loop_.dispatcher(), [this]() {
    zx_status_t status = StartFdWaiters();
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "failed to wait for first packet: " << status;
      loop_.Shutdown();
    }
  });
  if (status != ZX_OK) {
    return status;
  }
  status = loop_.Run(deadline, once);
  return status;
}
}  // namespace weavestack
