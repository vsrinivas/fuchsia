// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTING_COMPONENT_CONTEXT_H_
#define SRC_SYS_FUZZING_COMMON_TESTING_COMPONENT_CONTEXT_H_

#include <lib/zx/channel.h>
#include <stdint.h>

#include <memory>
#include <unordered_map>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "testing/fidl/async_loop_for_test.h"

namespace fuzzing {

// This class is a wrapper around |sys::ComponentContext| that provides some additional common
// behaviors, such as making an |async::Loop| and scheduling a primary task on an |async::Executor|.
class ComponentContextForTest final : public ComponentContext {
 public:
  ~ComponentContextForTest() override = default;

  // Creates a component context. Unlike the the base class, this method does not consumes any
  // startup handles. Instead, use |PutChannel| to add handles in order to serve FIDL protocols.
  static ComponentContextPtr Create();

  // Like |Create|, but does not have a loop and does not own its |executor|. This can be useful for
  // tests that provide an executor using a test loop dispatcher.
  static ComponentContextPtr Create(ExecutorPtr executor);

  // Adds a channel as if it had been passed as the |PA_HND(PA_USER0, arg)| startup handle.
  void PutChannel(uint32_t arg, zx::channel);

  // If |PutChannel| was called with the given |arg|, returns that channel; otherwise, returns an
  // invalid channel.
  zx::channel TakeChannel(uint32_t arg) override;

  // These use the test loop, if set. Otherwise, they dispatch to the base class.
  __WARN_UNUSED_RESULT zx_status_t Run() override;
  __WARN_UNUSED_RESULT zx_status_t RunUntilIdle() override;

 private:
  using ComponentContext::ComponentContext;

  std::unique_ptr<fidl::test::AsyncLoopForTest> loop_;
  std::unordered_map<uint32_t, zx::channel> channels_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(ComponentContextForTest);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTING_COMPONENT_CONTEXT_H_
