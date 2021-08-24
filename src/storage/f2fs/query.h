// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_F2FS_QUERY_H_
#define THIRD_PARTY_F2FS_QUERY_H_

namespace f2fs {

class F2fs;

class QueryService final : public fidl::WireServer<fuchsia_fs::Query>, public fs::Service {
 public:
  QueryService(async_dispatcher_t* dispatcher, F2fs* f2fs);

  void GetInfo(GetInfoRequestView request, GetInfoCompleter::Sync& completer) final;

  void IsNodeInFilesystem(IsNodeInFilesystemRequestView request,
                          IsNodeInFilesystemCompleter::Sync& completer) final;

 private:
  F2fs* const f2fs_;
};

}  // namespace f2fs

#endif  // THIRD_PARTY_F2FS_QUERY_H_
