// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_LOCATION_H_
#define SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_LOCATION_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/memfs/memfs.h>

#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/platform/unique_fd.h"

namespace ledger {

class FuchsiaScopedTmpLocation : public ScopedTmpLocation {
 public:
  FuchsiaScopedTmpLocation();
  ~FuchsiaScopedTmpLocation() override;

  FuchsiaScopedTmpLocation(const FuchsiaScopedTmpLocation&) = delete;
  FuchsiaScopedTmpLocation& operator=(const FuchsiaScopedTmpLocation&) = delete;

  DetachedPath path() override { return DetachedPath(root_fd_.get()); };

 private:
  async_loop_config_t config_;
  async::Loop loop_;
  memfs_filesystem_t* memfs_;
  unique_fd root_fd_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_LOCATION_H_
