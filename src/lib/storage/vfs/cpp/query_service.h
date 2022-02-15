// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_QUERY_SERVICE_H_
#define SRC_LIB_STORAGE_VFS_CPP_QUERY_SERVICE_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <fidl/fuchsia.fs/cpp/wire.h>

#include "src/lib/storage/vfs/cpp/service.h"

namespace fs {

class FuchsiaVfs;

class QueryService final : public fidl::WireServer<fuchsia_fs::Query>, public Service {
 public:
  explicit QueryService(FuchsiaVfs* vfs);

  // Query implementation.
  void IsNodeInFilesystem(IsNodeInFilesystemRequestView request,
                          IsNodeInFilesystemCompleter::Sync& completer) final;

 private:
  FuchsiaVfs* vfs_;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_QUERY_SERVICE_H_
