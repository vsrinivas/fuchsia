// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_memory_digest.h"

#include <zircon/status.h>

#include "harvester.h"
#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/digest.h"
#include "src/lib/fxl/logging.h"

namespace harvester {

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
  memory::Digester digester;
  memory::Digest digest(capture, &digester);

  SampleList list;
  for (auto const& bucket : digest.buckets()) {
    const auto& iter = name_to_path.find(bucket.name());
    if (iter == name_to_path.end()) {
      FXL_LOG(ERROR) << "Unknown bucket name: " << bucket.name();
      continue;
    }
    list.emplace_back(iter->second, bucket.size());
  }

  if (FXL_VLOG_IS_ON(3)) {
    FXL_VLOG(3) << "GatherMemoryDigest::Gather";
    for (auto const& item : list) {
      FXL_VLOG(3) << item.first << ": " << item.second;
    }
  }

  DockyardProxyStatus status = Dockyard().SendSampleList(list);
  if (status != DockyardProxyStatus::OK) {
    FXL_LOG(ERROR) << "SendSampleList failed (" << status << ")";
  }
}

}  // namespace harvester
