// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DEBUGDATA_DEBUGDATA_H_
#define LIB_DEBUGDATA_DEBUGDATA_H_

#include <fidl/fuchsia.debugdata/cpp/wire.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/cpp/bind.h>
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

// Publisher implements the |fuchsia.debugdata.Publisher| protocol. When a VMO
// is ready for processing it invokes the |vmo_callback| function.
// Publisher is not thread safe.
class Publisher : public fidl::WireServer<fuchsia_debugdata::Publisher> {
 private:
 public:
  using VmoHandler = fit::function<void(std::string, zx::vmo)>;

  explicit Publisher(async_dispatcher_t* dispatcher, fbl::unique_fd root_dir_fd,
                     VmoHandler vmo_callback);
  ~Publisher() override = default;

  void Publish(PublishRequestView request, PublishCompleter::Sync& completer) override;
  // Invoke |vmo_callback| on any outstanding VMOs, without waiting for the signal indicating the
  // VMO is ready.
  void DrainData();

  // Bind Debug data service using provided dispatcher.
  void BindDeprecatedDebugData(fidl::ServerEnd<fuchsia_debugdata::DebugData> server_end,
                               async_dispatcher_t* dispatcher = nullptr);

  // Bind Publisher service using provided dispatcher.
  void Bind(fidl::ServerEnd<fuchsia_debugdata::Publisher> server_end,
            async_dispatcher_t* dispatcher = nullptr);

 private:
  // This is deprecated. Kept for backward compatibility.
  // DebugData implements the |fuchsia.debugdata.DebugData| protocol. When a VMO
  // is ready for processing it invokes the |vmo_callback| function.
  // DebugData is not thread safe.
  class DebugData : public fidl::WireServer<fuchsia_debugdata::DebugData> {
   public:
    explicit DebugData(Publisher* parent);
    ~DebugData() override = default;

    void Publish(PublishRequestView request, PublishCompleter::Sync& completer) override;
    void LoadConfig(LoadConfigRequestView request, LoadConfigCompleter::Sync& completer) override;
    // Invoke |vmo_callback| on any outstanding VMOs, without waiting for the signal indicating the
    // VMO is ready.
    void DrainData() { parent_->DrainData(); }

   private:
    Publisher* parent_;
  };

  async_dispatcher_t* dispatcher_;
  std::list<std::tuple<std::shared_ptr<async::WaitOnce>, std::string, zx::vmo>> pending_handlers_;
  VmoHandler vmo_callback_;
  fbl::unique_fd root_dir_fd_;
  DebugData deprecated_debug_data_;
};

}  // namespace debugdata

#endif  // LIB_DEBUGDATA_DEBUGDATA_H_
