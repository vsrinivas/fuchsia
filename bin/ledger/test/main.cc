// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/tracing/lib/trace/provider.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

// Thread connecting to the environment to allow tracing tests.
class TracingThread {
 public:
  TracingThread() {
    thread_.Run();
    thread_.TaskRunner()->PostTask([this] { InitOnThread(); });
  }
  ~TracingThread() {
    thread_.TaskRunner()->PostTask([this] { QuitOnThread(); });
    thread_.Join();
  }

 private:
  void InitOnThread() {
    application_context_ =
        app::ApplicationContext::CreateFromStartupInfoNotChecked();
    if (application_context_->environment()) {
      tracing::InitializeTracer(application_context_.get(), {"ledger_tests"});
    }
  }
  void QuitOnThread() { mtl::MessageLoop::GetCurrent()->PostQuitTask(); }

  mtl::Thread thread_;
  std::unique_ptr<app::ApplicationContext> application_context_;
};

int main(int argc, char** argv) {
  TracingThread tracing_thread;

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
