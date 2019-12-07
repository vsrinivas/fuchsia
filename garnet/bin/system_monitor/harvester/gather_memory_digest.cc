// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_memory_digest.h"

#include <zircon/status.h>

#include "harvester.h"
#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/lib/fxl/logging.h"

// What is the verbose output level for trivia in this file. For easy debugging,
// change this value to 0 temporarily.
#define VERBOSE_FOR_FILE (3)

namespace harvester {

namespace {
// Helper to add a value to the sample |int_sample_list_|.
std::string KoidPath(zx_koid_t koid, const std::string& path) {
  std::ostringstream label;
  label << "koid:" << koid << ":" << path;
  return label.str();
}
}  // namespace

void GatherMemoryDigest::Gather() {
  // See src/developer/memory/metrics/digest.cc:13 kDefaultBucketMatches for a
  // list of bucket names.
  std::map<std::string, std::string> name_to_path = {
      {"ZBI Buffer", "memory_digest:zbi_buffer"},
      {"Graphics", "memory_digest:graphics"},
      {"Video Buffer", "memory_digest:video_buffer"},
      {"Fshost", "memory_digest:fs_host"},
      {"Minfs", "memory_digest:min_fs"},
      {"Blobfs", "memory_digest:blob_fs"},
      {"Flutter", "memory_digest:flutter"},
      {"Web", "memory_digest:web"},
      {"Kronk", "memory_digest:kronk"},
      {"Scenic", "memory_digest:scenic"},
      {"Amlogic", "memory_digest:amlogic"},
      {"Netstack", "memory_digest:net_stack"},
      {"Amber", "memory_digest:amber"},
      {"Pkgfs", "memory_digest:pkg_fs"},
      {"Cast", "memory_digest:cast"},
      {"Archivist", "memory_digest:archivist"},
      {"Cobalt", "memory_digest:cobalt"},

      // Special entries that are not part of kDefaultBucketMatches.
      {"Orphaned", "memory_digest:orphaned"},
      {"Kernel", "memory_digest:kernel"},
      {"Free", "memory_digest:free"},
  };

  memory::Capture capture;
  memory::Digest digest(capture, &digester_);
  memory::Summary summary(capture, &namer_, digest.undigested_vmos());

  SampleList list;
  StringSampleList strings;

  // Add digest samples.
  for (auto const& bucket : digest.buckets()) {
    const auto& iter = name_to_path.find(bucket.name());
    if (iter == name_to_path.end()) {
      FXL_LOG(ERROR) << "Unknown bucket name: " << bucket.name();
      continue;
    }
    list.emplace_back(iter->second, bucket.size());
  }

  // Add summary samples.
  for (auto const& process : summary.process_summaries()) {
    list.emplace_back(KoidPath(process.koid(), "summary:private_bytes"),
                      process.sizes().private_bytes);
    list.emplace_back(KoidPath(process.koid(), "summary:scaled_bytes"),
                      process.sizes().scaled_bytes);
    list.emplace_back(KoidPath(process.koid(), "summary:total_bytes"),
                      process.sizes().total_bytes);
    strings.emplace_back(KoidPath(process.koid(), "name"), process.name());
  }

  if (FXL_VLOG_IS_ON(VERBOSE_FOR_FILE)) {
    FXL_VLOG(VERBOSE_FOR_FILE) << "GatherMemoryDigest::Gather";
    for (auto const& item : list) {
      FXL_VLOG(VERBOSE_FOR_FILE) << item.first << ": " << item.second;
    }
    for (auto const& item : strings) {
      FXL_VLOG(VERBOSE_FOR_FILE) << item.first << ": " << item.second;
    }
  }

  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << DockyardErrorString("SendSampleList", status)
                   << " Memory digest and summary samples will be missing";
  }
  status = Dockyard().SendStringSampleList(strings);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << DockyardErrorString("SendStringSampleList", status)
                   << " Memory digest and summary names will be missing";
  }
}

}  // namespace harvester
