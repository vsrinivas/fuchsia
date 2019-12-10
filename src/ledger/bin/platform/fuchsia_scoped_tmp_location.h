// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_LOCATION_H_
#define SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_LOCATION_H_

#include "peridot/lib/scoped_tmpfs/scoped_tmpfs.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"

namespace ledger {

class FuchsiaScopedTmpLocation : public ScopedTmpLocation {
 public:
  FuchsiaScopedTmpLocation() = default;
  ~FuchsiaScopedTmpLocation() = default;

  FuchsiaScopedTmpLocation(const FuchsiaScopedTmpLocation&) = delete;
  FuchsiaScopedTmpLocation& operator=(const FuchsiaScopedTmpLocation&) = delete;

  DetachedPath path() override { return DetachedPath(scoped_tmp_fs_.root_fd()); };

 private:
  scoped_tmpfs::ScopedTmpFS scoped_tmp_fs_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_PLATFORM_FUCHSIA_SCOPED_TMP_LOCATION_H_
