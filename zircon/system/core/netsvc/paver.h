// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <optional>
#include <threads.h>

#include <limits.h>

#include <fbl/unique_fd.h>
#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/fzl/resizeable-vmo-mapper.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <tftp/tftp.h>
#include <zircon/types.h>

#include "tftp.h"

namespace netsvc {

class PaverInterface {
 public:
  virtual bool InProgress() = 0;
  virtual zx_status_t exit_code() = 0;
  virtual void reset_exit_code() = 0;

  // TODO: Explore returning an object which implements write and when it goes
  // out of scope, closes.
  virtual tftp_status OpenWrite(const char* filename, size_t size) = 0;
  virtual tftp_status Write(const void* data, size_t* length, off_t offset) = 0;
  virtual void Close() = 0;
};

class Paver : public PaverInterface {
 public:
  // Get the singleton instance.
  static Paver* Get();

  bool InProgress() final;
  zx_status_t exit_code() final;
  void reset_exit_code() final;

  tftp_status OpenWrite(const char* filename, size_t size) final;
  tftp_status Write(const void* data, size_t* length, off_t offset) final;
  void Close() final;

  // Visible for testing.
  explicit Paver(zx::channel svc_root, fbl::unique_fd devfs_root)
      : svc_root_(std::move(svc_root)), devfs_root_(std::move(devfs_root)) {}

  void set_timeout(zx::duration timeout) { timeout_ = timeout; }

 private:
  // Refer to //zircon/system/fidl/fuchsia.paver/paver.fidl for a list of what
  // these commands translate to.
  enum class Command {
    kAsset,
    kBootloader,
    kDataFile,
    kFvm,
  };

  uint8_t* buffer() { return static_cast<uint8_t*>(buffer_mapper_.start()); }

  zx_status_t WriteAsset(::llcpp::fuchsia::paver::DataSink::SyncClient data_sink,
                         ::llcpp::fuchsia::mem::Buffer buffer);

  // Pushes all data from the paver buffer (filled by netsvc) into the paver input VMO. When
  // there's no data to copy, blocks on data_ready until more data is written into the buffer.
  int StreamBuffer();

  // Monitors the vmo progress, and calls into paver service once finished.
  int MonitorBuffer();

  std::atomic<bool> in_progress_ = false;
  std::atomic<zx_status_t> exit_code_ = ZX_OK;

  // Total size of file
  size_t size_ = 0;

  // Paver command to call into.
  Command command_;

  // Channel to svc.
  zx::channel svc_root_;

  // File descriptor to dev.
  fbl::unique_fd devfs_root_;

  std::optional<::llcpp::fuchsia::paver::Paver::SyncClient> paver_svc_;

  union {
    // Only valid when command == Command::kAsset.
    struct {
      ::llcpp::fuchsia::paver::Configuration configuration_;
      ::llcpp::fuchsia::paver::Asset asset_;
    };
    // Only valid when command == Command::kDataFile.
    char path_[PATH_MAX];
  };

  // Buffer used for stashing data from tftp until it can be written out to the paver.
  fzl::ResizeableVmoMapper buffer_mapper_;
  // Buffer write offset.
  std::atomic<size_t> write_offset_ = 0;
  std::atomic<unsigned int> buf_refcount_ = 0;
  thrd_t buf_thrd_ = 0;
  sync_completion_t data_ready_;

  // Timeout monitor thread uses before timing out.
  zx::duration timeout_ = zx::sec(5 * TFTP_TIMEOUT_SECS);
};

}  // namespace netsvc
