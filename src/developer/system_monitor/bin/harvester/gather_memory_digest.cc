// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_memory_digest.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "harvester.h"
#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"

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

const std::map<std::string, std::string>& GetBucketMap() {
  // See src/developer/memory/metrics/digest.cc:13 kDefaultBucketMatches for a
  // list of bucket names.
  static std::map<std::string, std::string> name_to_path = {
      {"ZBI Buffer", "memory_digest:zbi_buffer"},
      {"Graphics", "memory_digest:graphics"},
      {"ContiguousPool", "memory_digest:contiguous_pool"},
      {"ProtectedPool", "memory_digest:protecte_pool"},
      {"Fshost", "memory_digest:fs_host"},
      {"Minfs", "memory_digest:min_fs"},
      {"Blobfs", "memory_digest:blob_fs"},
      {"BlobfsInactive", "memory_digest:blob_fs_inactive"},
      {"Flutter", "memory_digest:flutter"},
      {"FlutterApps", "memory_digest:flutter_apps"},
      {"Web", "memory_digest:web"},
      {"Kronk", "memory_digest:kronk"},
      {"Scenic", "memory_digest:scenic"},
      {"Amlogic", "memory_digest:amlogic"},
      {"Netstack", "memory_digest:net_stack"},
      {"Pkgfs", "memory_digest:pkg_fs"},
      {"Cast", "memory_digest:cast"},
      {"Archivist", "memory_digest:archivist"},
      {"Cobalt", "memory_digest:cobalt"},
      {"Audio", "memory_digest:audio"},
      {"Context", "memory_digest:context"},

      // Special entries that are not part of kDefaultBucketMatches.
      {"Orphaned", "memory_digest:orphaned"},
      {"Kernel", "memory_digest:kernel"},
      {"Free", "memory_digest:free"},
      {"Undigested", "memory_digest:undigested"},
  };
  return name_to_path;
}

void GatherMemoryDigest::Gather() {
  memory::Capture capture;
  memory::CaptureState capture_state;
  zx_status_t zx_status = memory::Capture::GetCaptureState(&capture_state);
  if (zx_status != ZX_OK) {
    FX_LOGS(ERROR) << ZxErrorString("GetCaptureState", zx_status)
                   << " Memory Digest will not be collected";
    return;
  }
  zx_status = memory::Capture::GetCapture(&capture, capture_state, memory::VMO);
  if (zx_status != ZX_OK) {
    FX_LOGS(ERROR) << ZxErrorString("GetCapture", zx_status)
                   << " Memory Digest will not be collected";
    return;
  }
  memory::Digest digest(capture, &digester_);
  memory::Summary summary(capture, &namer_, digest.undigested_vmos());

  SampleList list;
  StringSampleList strings;

  // Add digest samples.
  auto name_to_path = GetBucketMap();
  for (auto const& bucket : digest.buckets()) {
    const auto& iter = name_to_path.find(bucket.name());
    if (iter == name_to_path.end()) {
      FX_LOGS(ERROR) << "Unknown bucket name: " << bucket.name();
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

  if (FX_VLOG_IS_ON(VERBOSE_FOR_FILE)) {
    FX_VLOGS(VERBOSE_FOR_FILE) << "GatherMemoryDigest::Gather";
    for (auto const& item : list) {
      FX_VLOGS(VERBOSE_FOR_FILE) << item.first << ": " << item.second;
    }
    for (auto const& item : strings) {
      FX_VLOGS(VERBOSE_FOR_FILE) << item.first << ": " << item.second;
    }
  }

  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FX_LOGS(ERROR) << DockyardErrorString("SendSampleList", status)
                   << " Memory digest and summary samples will be missing";
  }
  status = Dockyard().SendStringSampleList(strings);
  if (status != DockyardProxyStatus::OK) {
    FX_LOGS(ERROR) << DockyardErrorString("SendStringSampleList", status)
                   << " Memory digest and summary names will be missing";
  }
}

}  // namespace harvester
