// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_VIRTUAL_CAMERA_STREAM_STORAGE_H_
#define SRC_CAMERA_BIN_VIRTUAL_CAMERA_STREAM_STORAGE_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <memory>

namespace camera {

// Used to store and retrieve information about streams and their data sources.
class StreamStorage {
 public:
  // Adds a stream config at |index|. If this is called with the same |index|
  // multiple times, older values will be overwritten.
  void SetStreamConfigAtIndex(size_t index, fuchsia::camera2::hal::StreamConfig stream_config);

  // Takes all the values added with |SetStreamConfigAtIndex| and returns them
  // as part of a |fuchsia::camera2::hal::Config| value. They will be ordered by
  // their |index| value as used in |SetStreamConfigAtIndex|. Any gaps between
  // indices will be removed. For example, calling SetStreamConfigAtIndex(0,
  // config) and then SetStreamConfigAtIndex(2, config_two) will cause this to
  // return a vector of two configs.
  fuchsia::camera2::hal::Config GetConfig();

 private:
  // Stores all |StreamConfig| values added with |SetStreamConfigAtIndex|. An
  // empty optional value indicates nothing has been added at that index yet.
  std::vector<std::optional<fuchsia::camera2::hal::StreamConfig>> stream_configs_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_VIRTUAL_CAMERA_STREAM_STORAGE_H_
