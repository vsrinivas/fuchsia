// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DEBUGDATA_DEBUGDATA_H_
#define LIB_DEBUGDATA_DEBUGDATA_H_

#include <fuchsia/debugdata/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdint.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>
#include <fbl/vector.h>

namespace debugdata {

class DebugData : public fidl::WireServer<fuchsia_debugdata::DebugData> {
 public:
  explicit DebugData(fbl::unique_fd root_dir_fd);
  ~DebugData() = default;

  void Publish(PublishRequestView request, PublishCompleter::Sync& completer) override;
  void LoadConfig(LoadConfigRequestView request, LoadConfigCompleter::Sync& completer) override;

  // Wait for DebugData publishers to indicate vmos are ready, then take data.
  // Note this may wait indefinitely if any publishing processes are active and have not closed
  // their control channels passed through DebugData::Publish.
  std::unordered_map<std::string, std::vector<zx::vmo>> TakeData();

 private:
  std::unordered_map<std::string, std::vector<zx::vmo>> data_ __TA_GUARDED(lock_);
  std::vector<zx::channel> vmo_token_channels_ __TA_GUARDED(lock_);
  std::mutex lock_;
  fbl::unique_fd root_dir_fd_;
};

}  // namespace debugdata

#endif  // LIB_DEBUGDATA_DEBUGDATA_H_
