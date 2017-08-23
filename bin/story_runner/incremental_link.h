// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_STORY_RUNNER_INCREMENTAL_LINK_H_
#define APPS_MODULAR_SRC_STORY_RUNNER_INCREMENTAL_LINK_H_

#include <string>

#include "apps/modular/lib/fidl/json_xdr.h"
#include "apps/modular/services/story/link_change.fidl.h"
#include "apps/modular/src/story_runner/link_impl.h"

namespace modular {

class LinkImpl::IncrementalWriteCall : Operation<> {
 public:
  IncrementalWriteCall(OperationContainer* container,
            LinkImpl* impl,
            LinkChangePtr data,
            ResultCall result_call);

  std::string key();

 private:
  void Run() override;

  LinkImpl* const impl_;  // not owned
  LinkChangePtr data_;

  FTL_DISALLOW_COPY_AND_ASSIGN(IncrementalWriteCall);
};

class LinkImpl::IncrementalChangeCall : Operation<> {
 public:
  IncrementalChangeCall(OperationContainer* const container,
                        LinkImpl* const impl, LinkChangePtr data, uint32_t src);

 private:
  void Run() override;
  void Cont1(FlowToken flow, uint32_t src);

  LinkImpl* const impl_;  // not owned
  LinkChangePtr data_;
  std::string old_json_;
  uint32_t src_;

  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(IncrementalChangeCall);
};

// Reload needs to run if:
// 1. LinkImpl was just constructed
// 2. IncrementalChangeCall sees an out-of-order change
class LinkImpl::ReloadCall : Operation<> {
 public:
  ReloadCall(OperationContainer* container,
            LinkImpl* impl,
            ResultCall result_call);

 private:
  void Run() override;

  void Cont1(FlowToken flow, fidl::Array<LinkChangePtr> changes);

  LinkImpl* const impl_;  // not owned
  std::string json_;

  // WriteCall is executed here.
  OperationQueue operation_queue_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ReloadCall);
};


}  // namespace modular

#endif  // APPS_MODULAR_SRC_STORY_RUNNER_INCREMENTAL_LINK_H_
