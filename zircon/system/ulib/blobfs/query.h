// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_QUERY_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_QUERY_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fuchsia/fs/llcpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>

#include <fs/service.h>

namespace blobfs {

class Blobfs;
class Runner;

class QueryService final : public llcpp::fuchsia::fs::Query::Interface, public fs::Service {
 public:
  explicit QueryService(async_dispatcher_t* dispatcher, Blobfs* blobfs, Runner* runner);

  void GetInfo(llcpp::fuchsia::fs::FilesystemInfoQuery query,
               GetInfoCompleter::Sync completer) final;

  void IsNodeInFilesystem(zx::event token, IsNodeInFilesystemCompleter::Sync completer) final;

 private:
  const Blobfs* const blobfs_;
  Runner* const runner_;
};

}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_QUERY_H_
