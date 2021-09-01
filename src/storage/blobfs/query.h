// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOBFS_QUERY_H_
#define SRC_STORAGE_BLOBFS_QUERY_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fidl/fuchsia.fs/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>

#include "src/lib/storage/vfs/cpp/service.h"

namespace blobfs {

class Blobfs;
class Runner;

class QueryService final : public fidl::WireServer<fuchsia_fs::Query>, public fs::Service {
 public:
  QueryService(async_dispatcher_t* dispatcher, Blobfs* blobfs, Runner* runner);

  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) final;

  void IsNodeInFilesystem(IsNodeInFilesystemRequestView request,
                          IsNodeInFilesystemCompleter::Sync& completer) final;

 private:
  const Blobfs* const blobfs_;
  Runner* const runner_;
};

}  // namespace blobfs

#endif  // SRC_STORAGE_BLOBFS_QUERY_H_
