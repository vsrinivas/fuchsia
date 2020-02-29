// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/weave/weavestack/app.h"

#include <lib/syslog/cpp/logger.h>

#include <Weave/DeviceLayer/PlatformManager.h>

namespace weavestack {

namespace {
using nl::Weave::DeviceLayer::PlatformMgr;
using nl::Weave::DeviceLayer::PlatformMgrImpl;
}  // namespace

App::App() = default;

App::~App() {
  Quit();
}

void App::Quit()
{
  running_.clear();
  PlatformMgrImpl().GetSystemLayer().WakeSelect();
  Join();
}

// TODO(fxb/47096): tracks the integration test.
WEAVE_ERROR App::HandlePackets(void) {
  int numFDs = 0;
  fd_set readFDs, writeFDs, exceptFDs;
  FD_ZERO(&readFDs);
  FD_ZERO(&writeFDs);
  FD_ZERO(&exceptFDs);

  struct timeval sleepTime;
  sleepTime.tv_sec = 0;
  sleepTime.tv_usec = 0;

  PlatformMgrImpl().GetSystemLayer().PrepareSelect(numFDs, &readFDs, &writeFDs, &exceptFDs, sleepTime);
  PlatformMgrImpl().GetInetLayer().PrepareSelect(numFDs, &readFDs, &writeFDs, &exceptFDs, sleepTime);

  int res = select(numFDs, &readFDs, &writeFDs, &exceptFDs, &sleepTime);
  if (res < 0) {
    FX_LOGS(ERROR) << "select failed: " << strerror(errno);
    return WEAVE_ERROR_CONNECTION_ABORTED;
  }
  PlatformMgrImpl().GetSystemLayer().HandleSelectResult(res, &readFDs, &writeFDs, &exceptFDs);
  PlatformMgrImpl().GetInetLayer().HandleSelectResult(res, &readFDs, &writeFDs, &exceptFDs);

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR App::Init() {
  WEAVE_ERROR err;

  syslog::InitLogger({"weavestack"});

  err = PlatformMgr().InitWeaveStack();
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "InitWeaveStack() failed " << nl::ErrorStr(err);
    return err;
  }
  return WEAVE_NO_ERROR;
}

void App::RunLoop() {
  while (running_.test_and_set()) {
    WEAVE_ERROR ret = HandlePackets();
    if (ret != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "HandlePackets() failed " << ret;
      break;
    }
  }
  running_.clear();
}

void App::Join() {
  if (thread_.joinable()) {
    thread_.join();
  }
}

WEAVE_ERROR App::Start() {
  running_.test_and_set();
  thread_ = std::thread{&App::RunLoop, this};
  return WEAVE_NO_ERROR;
}

}  // namespace weavestack
