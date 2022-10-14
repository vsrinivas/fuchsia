// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_DOWNLOAD_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_DOWNLOAD_OBSERVER_H_

#include <stddef.h>

namespace zxdb {

class DownloadObserver {
 public:
  // Called when a download is first created and activated.
  virtual void OnDownloadsStarted() {}

  // Called when a download is done.
  virtual void OnDownloadsStopped(size_t num_succeeded, size_t num_failed) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_DOWNLOAD_OBSERVER_H_
