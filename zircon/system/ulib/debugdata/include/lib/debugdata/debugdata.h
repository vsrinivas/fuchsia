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

#include <list>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <fbl/unique_fd.h>
#include <fbl/vector.h>

namespace debugdata {

// DebugData implements the |fuchsia.debugdata.DebugData| protocol. When a VMO
// is ready for processing it invokes the |vmo_callback| function.
// DebugData is not thread safe.
class DebugData : public fidl::WireServer<fuchsia_debugdata::DebugData> {
 public:
  using VmoHandler = fit::function<void(std::string, zx::vmo)>;

  explicit DebugData(async_dispatcher_t* dispatcher, fbl::unique_fd root_dir_fd,
                     VmoHandler vmo_callback);
  ~DebugData() = default;

  void Publish(PublishRequestView request, PublishCompleter::Sync& completer) override;
  void LoadConfig(LoadConfigRequestView request, LoadConfigCompleter::Sync& completer) override;
  // Invoke |vmo_callback| on any outstanding VMOs, without waiting for the signal indicating the
  // VMO is ready.
  void DrainData();

 private:
  async_dispatcher_t* dispatcher_;
  std::list<std::tuple<std::shared_ptr<async::WaitOnce>, std::string, zx::vmo>> pending_handlers_;
  VmoHandler vmo_callback_;
  fbl::unique_fd root_dir_fd_;
};

}  // namespace debugdata

#endif  // LIB_DEBUGDATA_DEBUGDATA_H_
