// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_ADMIN_H_
#define SRC_STORAGE_F2FS_ADMIN_H_

namespace f2fs {

class AdminService final : public fidl::WireServer<fuchsia_fs::Admin>, public fs::Service {
 public:
  AdminService(async_dispatcher_t* dispatcher, F2fs* f2fs);

  void Shutdown(ShutdownRequestView request, ShutdownCompleter::Sync& completer) final;
  void GetRoot(GetRootRequestView request, GetRootCompleter::Sync& completer) final;

 private:
  F2fs* const f2fs_;
};

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_ADMIN_H_
