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

class DebugData : public ::llcpp::fuchsia::debugdata::DebugData::Interface {
 public:
  explicit DebugData(fbl::unique_fd root_dir_fd);
  ~DebugData() = default;

  void Publish(fidl::StringView data_sink, zx::vmo vmo, PublishCompleter::Sync& completer) override;
  void LoadConfig(fidl::StringView config_name, LoadConfigCompleter::Sync& completer) override;

  const auto& data() const { return data_; }

 private:
  std::unordered_map<std::string, std::vector<zx::vmo>> data_ __TA_GUARDED(lock_);
  std::mutex lock_;
  fbl::unique_fd root_dir_fd_;
};

}  // namespace debugdata

#endif  // LIB_DEBUGDATA_DEBUGDATA_H_
