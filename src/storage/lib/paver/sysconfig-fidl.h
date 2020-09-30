// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_STORAGE_LIB_PAVER_SYSCONFIG_FIDL_H_
#define SRC_STORAGE_LIB_PAVER_SYSCONFIG_FIDL_H_

#include <fuchsia/paver/llcpp/fidl.h>
#include <lib/zx/channel.h>

#include "partition-client.h"
#include "paver-context.h"

namespace paver {

class Sysconfig : public ::llcpp::fuchsia::paver::Sysconfig::Interface {
 public:
  explicit Sysconfig(std::unique_ptr<PartitionClient> client) : partitioner_(std::move(client)) {}

  static void Bind(async_dispatcher_t* dispatcher, fbl::unique_fd devfs_root, zx::channel svc_root,
                   std::shared_ptr<Context> context, zx::channel server);

  void Read(ReadCompleter::Sync& completer) override;

  void Write(::llcpp::fuchsia::mem::Buffer payload, WriteCompleter::Sync& completer) override;

  void GetPartitionSize(GetPartitionSizeCompleter::Sync& completer) override;

  void Flush(FlushCompleter::Sync& completer) override;

  void Wipe(WipeCompleter::Sync& completer) override;

 private:
  std::unique_ptr<PartitionClient> partitioner_;
};

}  // namespace paver

#endif  // SRC_STORAGE_LIB_PAVER_SYSCONFIG_FIDL_H_
