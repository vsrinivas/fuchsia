// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <fuchsia/debugdata/llcpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zircon-internal/fnv1hash.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <stdint.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace debugdata {

struct DataSinkDump {
  std::string sink_name;
  zx::vmo file_data;
};

class DebugData : public ::llcpp::fuchsia::debugdata::DebugData::Interface {
 public:
  explicit DebugData(fbl::unique_fd root_dir_fd);
  ~DebugData() = default;

  void Publish(fidl::StringView data_sink, zx::vmo vmo, PublishCompleter::Sync completer) override;
  void LoadConfig(fidl::StringView config_name, LoadConfigCompleter::Sync completer) override;

  std::vector<DataSinkDump>& GetData() { return data_; }

 private:
  std::vector<DataSinkDump> data_;
  std::mutex lock_;
  fbl::unique_fd root_dir_fd_;
};

}  // namespace debugdata
