// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/josh/lib/fxlog.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/memfs/memfs.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "src/developer/shell/josh/lib/js_testing_utils.h"
#include "third_party/quickjs/quickjs.h"

namespace fs = std::filesystem;

namespace shell {

class LogReader : public fuchsia::logger::LogListenerSafe {
 public:
  LogReader(uint32_t collect_count, fit::function<void()> all_done)
      : collect_count_(collect_count), all_done_(std::move(all_done)), binding_(this) {
    binding_.Bind(log_listener_.NewRequest());
  }

  bool Connect(sys::ComponentContext* component_context) {
    if (!log_listener_) {
      return false;
    }

    // Get current process koid
    zx_info_handle_basic_t info;
    zx_status_t status = zx_object_get_info(zx::process::self()->get(), ZX_INFO_HANDLE_BASIC, &info,
                                            sizeof(info), nullptr, nullptr);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    auto log_service = component_context->svc()->Connect<fuchsia::logger::Log>();
    auto options = fuchsia::logger::LogFilterOptions::New();
    options->filter_by_pid = true;
    options->pid = info.koid;
    options->min_severity = fuchsia::logger::LogLevelFilter::TRACE;
    // make tags non-null.
    options->tags.resize(0);
    log_service->DumpLogsSafe(std::move(log_listener_), std::move(options));
    return true;
  }

  void LogMany(::std::vector<fuchsia::logger::LogMessage> Log, LogManyCallback received) override {
    if (collect_count_ > 0) {
      for (auto& entry : Log) {
        messages.emplace_back(entry);
        if (--collect_count_ == 0)
          break;
      }
    }
    if (collect_count_ == 0) {
      all_done_();
    }
    received();
  }

  void Log(fuchsia::logger::LogMessage Log, LogCallback received) override {
    if (collect_count_ > 0) {
      fuchsia::logger::LogMessage msg;
      Log.Clone(&msg);
      messages.push_back(std::move(msg));

      collect_count_--;
    }
    if (collect_count_ == 0) {
      all_done_();
    }
    received();
  }

  void Done() override { all_done_(); }

  std::vector<fuchsia::logger::LogMessage> messages;

 protected:
  int collect_count_;
  fit::function<void()> all_done_;
  ::fidl::Binding<fuchsia::logger::LogListenerSafe> binding_;
  fuchsia::logger::LogListenerSafePtr log_listener_;
};

class FxLogTest : public JsTest {
 protected:
  void SetUp() override {
    JsTest::SetUp();

    // Always enable STD libraries
    if (!ctx_->InitStd()) {
      ctx_->DumpError();
      FAIL();
    }

    // Builins should have fxlog setup
    InitBuiltins("/pkg/data/fidling", "/pkg/data/lib");

    // Enable temp filesystem
    loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop_->StartThread(), ZX_OK);
    ASSERT_EQ(ZX_OK, memfs_install_at(loop_->dispatcher(), "/test_tmp", &fs_));

    // Make sure file creation is OK so memfs is running OK.
    char tmpfs_test_file[] = "/test_tmp/runtime.test.XXXXXX";
    ASSERT_NE(mkstemp(tmpfs_test_file), -1);
  }

  void TearDown() override {
    // Synchronously clean up.
    sync_completion_t unmounted;
    memfs_free_filesystem(fs_, &unmounted);
    sync_completion_wait(&unmounted, zx::duration::infinite().get());
    fs_ = nullptr;

    loop_->Shutdown();
  }

  std::unique_ptr<LogReader> CollectLog(uint32_t maximum_entry) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    loop.StartThread();

    std::unique_ptr<LogReader> log_reader;
    async::PostTask(loop.dispatcher(), [&log_reader, &loop, &maximum_entry] {
      async_set_default_dispatcher(loop.dispatcher());

      log_reader = std::make_unique<LogReader>(maximum_entry, [&loop] {
        // Done parsing the log
        loop.Quit();
      });

      auto component_context = sys::ComponentContext::CreateAndServeOutgoingDirectory();
      log_reader->Connect(component_context.get());
    });
    loop.Run();
    loop.JoinThreads();

    return log_reader;
  }

  std::unique_ptr<async::Loop> loop_;
  memfs_filesystem_t* fs_;
};

TEST_F(FxLogTest, TestEvalLog) {
  // Validate the results
  ASSERT_EQ(true, Eval(R"(
    function my_func() { // line2
      fxlog.error("Message3");
      fxlog.info("Message4", "TestTag2");
    }
    fxlog.info("Message1");
    fxlog.warn("Message2", "TestTag");
    my_func();
  )"));
  loop_->RunUntilIdle();

  auto reader = CollectLog(4);
  ASSERT_EQ(reader->messages.size(), 4u);

  ASSERT_EQ(reader->messages[0].msg, "[batch(6)] Message1");
  ASSERT_EQ(reader->messages[1].msg, "[batch(7)] Message2");
  ASSERT_EQ(reader->messages[2].msg, "[batch(3)] Message3");
  ASSERT_EQ(reader->messages[3].msg, "[batch(4)] Message4");

  ASSERT_EQ(reader->messages[0].tags[0], "<eval>");
  ASSERT_EQ(reader->messages[1].tags[0], "TestTag");
  ASSERT_EQ(reader->messages[2].tags[0], "my_func");
  ASSERT_EQ(reader->messages[3].tags[0], "TestTag2");

  ASSERT_EQ(reader->messages[0].severity, (int32_t)fuchsia::logger::LogLevelFilter::INFO);
  ASSERT_EQ(reader->messages[1].severity, (int32_t)fuchsia::logger::LogLevelFilter::WARN);
  ASSERT_EQ(reader->messages[2].severity, (int32_t)fuchsia::logger::LogLevelFilter::ERROR);
  ASSERT_EQ(reader->messages[3].severity, (int32_t)fuchsia::logger::LogLevelFilter::INFO);
}

TEST_F(FxLogTest, TestScriptLog) {
  std::ofstream test_script;

  test_script.open("/test_tmp/test_log.js");
  // Cannot use "error" or above otherwise gtest will treat the test
  // as failed
  test_script << R"(
    // FX log test script. (line2)
    function my_func() {
      fxlog.error("Message3");
      fxlog.info("Message4", "TestTag2");
    }
    fxlog.info("Message1");
    fxlog.warn("Message2", "TestTag");
    my_func();
  )";
  test_script.close();

  // Validate the results
  ASSERT_EQ(true, Eval(R"(
      std.loadScript("/test_tmp/test_log.js")
    )"));
  loop_->RunUntilIdle();

  auto reader = CollectLog(4);
  ASSERT_EQ(reader->messages.size(), 4u);

  ASSERT_EQ(reader->messages[0].msg, "[test_log.js(7)] Message1");
  ASSERT_EQ(reader->messages[1].msg, "[test_log.js(8)] Message2");
  ASSERT_EQ(reader->messages[2].msg, "[test_log.js(4)] Message3");
  ASSERT_EQ(reader->messages[3].msg, "[test_log.js(5)] Message4");

  ASSERT_EQ(reader->messages[0].tags[0], "<eval>");
  ASSERT_EQ(reader->messages[1].tags[0], "TestTag");
  ASSERT_EQ(reader->messages[2].tags[0], "my_func");
  ASSERT_EQ(reader->messages[3].tags[0], "TestTag2");

  ASSERT_EQ(reader->messages[0].severity, (int32_t)fuchsia::logger::LogLevelFilter::INFO);
  ASSERT_EQ(reader->messages[1].severity, (int32_t)fuchsia::logger::LogLevelFilter::WARN);
  ASSERT_EQ(reader->messages[2].severity, (int32_t)fuchsia::logger::LogLevelFilter::ERROR);
  ASSERT_EQ(reader->messages[3].severity, (int32_t)fuchsia::logger::LogLevelFilter::INFO);
}

}  // namespace shell
