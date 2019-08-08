// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/high_water.h"

#include <lib/async/dispatcher.h>
#include <zircon/status.h>

#include <fstream>

#include <src/lib/files/file.h>
#include <src/lib/fxl/logging.h>

#include "src/developer/memory/metrics/printer.h"

namespace monitor {

using namespace memory;

HighWater::HighWater(const std::string& dir, zx::duration poll_frequency,
                     uint64_t high_water_threshold, async_dispatcher_t* dispatcher,
                     fit::function<zx_status_t(Capture&, CaptureLevel)> capture_cb)
    : dir_(dir),
      watcher_(poll_frequency, high_water_threshold, dispatcher, std::move(capture_cb),
               [this](const Capture& c) { RecordHighWater(c); }),
      namer_(Summary::kNameMatches) {
  // Ok to ignore result. last might not exist.
  remove((dir_ + "/previous.txt").c_str());
  // Ok to ignore this too. Latest might not exist.
  rename((dir_ + "/latest.txt").c_str(), (dir_ + "/previous.txt").c_str());
}

void HighWater::RecordHighWater(const Capture& capture) {
  Summary s(capture, &namer_);
  std::ofstream out;
  out.open(dir_ + "/latest.txt");
  Printer p(out);
  p.PrintSummary(s, VMO, memory::SORTED);
  out.close();
}

std::string HighWater::GetHighWater() const {
  std::string high_water;
  if (files::ReadFileToString(dir_ + "/latest.txt", &high_water)) {
    return high_water;
  }
  return "";
}

std::string HighWater::GetPreviousHighWater() const {
  std::string high_water;
  if (files::ReadFileToString(dir_ + "/previous.txt", &high_water)) {
    return high_water;
  }
  return "";
}

}  // namespace monitor
