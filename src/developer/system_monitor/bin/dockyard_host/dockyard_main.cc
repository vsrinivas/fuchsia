// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <thread>

#include "src/developer/system_monitor/bin/dockyard_host/dockyard_host.h"
#include "src/developer/system_monitor/lib/gt_log.h"

int main(int argc, const char* const* argv) {
  if (!gt::SetUpLogging(argc, argv)) {
    GT_LOG(FATAL) << "Invalid command line arguments.";
    exit(1);
  }
  GT_LOG(INFO) << "Starting dockyard host";

  DockyardHost host;
  host.StartCollectingFrom("");
  while (true) {
    // In a later version of this code we will do real work here.
    std::this_thread::sleep_for(std::chrono::seconds(1));
    host.Dockyard().ProcessRequests();
  }
}
