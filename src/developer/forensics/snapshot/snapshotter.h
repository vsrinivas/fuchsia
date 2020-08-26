// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_SNAPSHOT_SNAPSHOTTER_H_
#define SRC_DEVELOPER_FORENSICS_SNAPSHOT_SNAPSHOTTER_H_

#include <lib/sys/cpp/service_directory.h>

#include <memory>

namespace forensics {
namespace snapshot {

// Dumps an archive file containing all the feedback data collected from
// fuchsia.feedback.DataProvider into stdout or to |out_filename| if not-null.
//
// fuchsia.feedback.DataProvider is expected to be in |services|.
bool MakeSnapshot(std::shared_ptr<sys::ServiceDirectory> services,
                  const char* out_filename = nullptr);

}  // namespace snapshot
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_SNAPSHOT_SNAPSHOTTER_H_
