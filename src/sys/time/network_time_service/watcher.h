// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_TIME_NETWORK_TIME_SERVICE_WATCHER_H_
#define SRC_SYS_TIME_NETWORK_TIME_SERVICE_WATCHER_H_

#include <fuchsia/time/external/cpp/fidl.h>

namespace time_external = fuchsia::time::external;
namespace network_time_service {

// A hanging get handler that allows parking callbacks, then invoking them
// later when a new value is available. This class is not thread safe.
class SampleWatcher {
 public:
  SampleWatcher() {}

  // Register a callback that is executed when a new sample is produced and
  // return if successful. Returns false without registering the callback if
  // another callback is already registered.
  bool Watch(time_external::PushSource::WatchSampleCallback callback);

  // Push a new sample. Any registered callback is invoked if the sample has changed.
  void Update(time_external::TimeSample new_sample);

  // Clears any registered callback and last sent state.
  void ResetClient();

 private:
  time_external::TimeSample CloneTimeSample(const time_external::TimeSample& sample);

  void CallbackIfNeeded();

  std::optional<time_external::PushSource::WatchSampleCallback> callback_;
  std::optional<time_external::TimeSample> last_sent_;
  std::optional<time_external::TimeSample> current_;
};

}  // namespace network_time_service

#endif  // SRC_SYS_TIME_NETWORK_TIME_SERVICE_WATCHER_H_
