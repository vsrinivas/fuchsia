// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/high_water.h"

#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <fstream>

#include <src/lib/files/file.h>
#include <src/lib/files/path.h>

#include "src/developer/memory/metrics/digest.h"
#include "src/developer/memory/metrics/printer.h"

namespace monitor {

using namespace memory;

static const char kLatest[] = "latest.txt";
static const char kPrevious[] = "previous.txt";
static const char kLatestDigest[] = "latest_digest.txt";
static const char kPreviousDigest[] = "previous_digest.txt";

HighWater::HighWater(const std::string& dir, zx::duration poll_frequency,
                     uint64_t high_water_threshold, async_dispatcher_t* dispatcher,
                     CaptureFn capture_cb, DigestCb digest_cb)
    : dir_(dir),
      watcher_(poll_frequency, high_water_threshold, dispatcher, std::move(capture_cb),
               [this](const Capture& c) {
                 RecordHighWater(c);
                 RecordHighWaterDigest(c);
               }),
      namer_(Summary::kNameMatches),
      digest_cb_(std::move(digest_cb)) {
  // Ok to ignore result. last might not exist.
  remove(files::JoinPath(dir_, kPrevious).c_str());
  remove(files::JoinPath(dir_, kPreviousDigest).c_str());
  // Ok to ignore this too. Latest might not exist.
  rename(files::JoinPath(dir_, kLatest).c_str(), files::JoinPath(dir_, kPrevious).c_str());
  rename(files::JoinPath(dir_, kLatestDigest).c_str(),
         files::JoinPath(dir_, kPreviousDigest).c_str());
}

void HighWater::RecordHighWater(const Capture& capture) {
  Summary s(capture, &namer_);
  std::ofstream out;
  out.open(files::JoinPath(dir_, kLatest));
  Printer p(out);
  p.PrintSummary(s, VMO, memory::SORTED);
  out.close();
}

void HighWater::RecordHighWaterDigest(const Capture& capture) {
  Digest digest;
  digest_cb_(capture, &digest);
  std::ofstream out;
  out.open(files::JoinPath(dir_, kLatestDigest));
  Printer p(out);
  p.PrintDigest(digest);
  out.close();
}

std::string HighWater::GetHighWater() const { return GetFile(kLatest); }
std::string HighWater::GetPreviousHighWater() const { return GetFile(kPrevious); }
std::string HighWater::GetHighWaterDigest() const { return GetFile(kLatestDigest); }
std::string HighWater::GetPreviousHighWaterDigest() const { return GetFile(kPreviousDigest); }

std::string HighWater::GetFile(const char* filename) const {
  std::string file;
  if (files::ReadFileToString(files::JoinPath(dir_, filename), &file)) {
    return file;
  }
  return "";
}

}  // namespace monitor
