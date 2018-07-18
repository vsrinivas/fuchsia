// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_PTS_MANAGER_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_PTS_MANAGER_H_

#include <cstdint>
#include <map>
#include <mutex>

#include "lib/fxl/synchronization/thread_annotations.h"

class PtsManager {
 public:
  // Offset is the byte offset into the stream of the beginning of the frame.
  void InsertPts(uint64_t offset, uint64_t pts);

  // Offset must be within the frame that's being looked up. Only the last 100
  // PTS inserted are kept around.
  bool LookupPts(uint64_t offset, uint64_t* pts_out);

 private:
  std::mutex lock_;
  FXL_GUARDED_BY(lock_)
  std::map<uint64_t, uint64_t> pts_list_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_PTS_MANAGER_H_
