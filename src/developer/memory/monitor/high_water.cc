// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/high_water.h"

#include <fcntl.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>
#include <zircon/status.h>

#include <fstream>
#include <mutex>

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
  std::lock_guard<std::mutex> lock(mutex_);
  Summary s(capture, &namer_);
  std::ofstream out;
  auto path = files::JoinPath(dir_, kLatest);
  out.open(path);
  Printer p(out);
  p.PrintSummary(s, VMO, memory::SORTED);
  out.close();

  // Force a sync to filesystem.
  auto out_fd = open(path.c_str(), O_WRONLY);
  fsync(out_fd);
  close(out_fd);
}

void HighWater::RecordHighWaterDigest(const Capture& capture) {
  std::lock_guard<std::mutex> lock(mutex_);
  Digest digest;
  digest_cb_(capture, &digest);
  std::ofstream out;
  auto path = files::JoinPath(dir_, kLatestDigest);
  out.open(path);
  Printer p(out);
  p.PrintDigest(digest);
  out.close();

  // Force a sync to filesystem.
  auto out_fd = open(path.c_str(), O_WRONLY);
  fsync(out_fd);
  close(out_fd);
}

std::string HighWater::GetHighWater() { return GetFile(kLatest); }
std::string HighWater::GetPreviousHighWater() { return GetFile(kPrevious); }
std::string HighWater::GetHighWaterDigest() { return GetFile(kLatestDigest); }
std::string HighWater::GetPreviousHighWaterDigest() { return GetFile(kPreviousDigest); }

std::string HighWater::GetFile(const char* filename) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string file;
  if (files::ReadFileToString(files::JoinPath(dir_, filename), &file)) {
    return file;
  }
  return "";
}

}  // namespace monitor
