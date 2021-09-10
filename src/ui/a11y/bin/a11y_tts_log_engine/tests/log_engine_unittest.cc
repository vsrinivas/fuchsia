// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_tts_log_engine/log_engine.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>

#include <list>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "macros.h"
#include "src/lib/diagnostics/accessor2logger/log_message.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace a11y {
namespace {
using gtest::RealLoopFixture;

class LogEngineTest : public RealLoopFixture {
 protected:
  LogEngineTest() = default;
  ~LogEngineTest() override = default;

  void SetUp() override {
    startup_context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();
    persistent_context_ = startup_context_.get();
    tts_manager_ = std::make_unique<TtsManager>(startup_context_.get());
  }
  void Iterate(const std::shared_ptr<fuchsia::diagnostics::BatchIteratorPtr>& iterator) {
    (*iterator)->GetNext([=](::fuchsia::diagnostics::BatchIterator_GetNext_Result result) {
      auto content = std::move(result.response().batch);
      for (auto& bot : content) {
        auto values =
            diagnostics::accessor2logger::ConvertFormattedContentToLogMessages(std::move(bot))
                .value();
        for (auto& msg : values) {
          log_output_ << msg.value().msg;
        }
      }
      std::vector<std::list<std::string>::iterator> to_delete;
      for (auto it = expects_.begin(); it != expects_.end(); it++) {
        if (log_output_.str().find(*it) != std::string::npos) {
          to_delete.push_back(it);
        }
      }
      for (auto i : to_delete) {
        expects_.erase(i);
      }
      if (expects_.empty()) {
        QuitLoop();
        return;
      }
      Iterate(iterator);
    });
  }

  // Initializes the global logger.
  //
  // This redirects the output to the socket pair |log_socket_local_| and |log_socket_remote_|
  // so that we can capture it.
  zx_status_t InitLogger() {
    std::shared_ptr<fuchsia::diagnostics::BatchIteratorPtr> iterator =
        std::make_shared<fuchsia::diagnostics::BatchIteratorPtr>();
    auto res = persistent_context_->svc()->Connect(accessor_.NewRequest(dispatcher()));
    ::fuchsia::diagnostics::StreamParameters params;
    params.set_client_selector_configuration(
        fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));
    params.set_data_type(fuchsia::diagnostics::DataType::LOGS);
    params.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT_THEN_SUBSCRIBE);
    params.set_format(fuchsia::diagnostics::Format::JSON);
    accessor_->StreamDiagnostics(std::move(params), iterator->NewRequest(dispatcher()));
    Iterate(iterator);
    return ZX_OK;
  }

  void Expect(std::string&& log_message) { expects_.push_back(std::move(log_message)); }

  bool GotExpected() { return expects_.empty(); }

  bool LogContains(std::string&& msg) {
    expects_.push_back(std::move(msg));
    InitLogger();
    RunLoop();
    return expects_.empty();
  }
  fidl::InterfacePtr<fuchsia::logger::Log> log_service_;
  fidl::InterfacePtr<fuchsia::diagnostics::ArchiveAccessor> accessor_;
  std::list<std::string> expects_;
  sys::ComponentContext* persistent_context_ = nullptr;
  std::unique_ptr<sys::ComponentContext> startup_context_;
  std::unique_ptr<TtsManager> tts_manager_;
  std::stringstream log_output_;
};

TEST_F(LogEngineTest, OutputsLogs) {
  fuchsia::accessibility::tts::EnginePtr speaker;
  tts_manager_->OpenEngine(speaker.NewRequest(),
                           [](fuchsia::accessibility::tts::TtsManager_OpenEngine_Result result) {
                             EXPECT_TRUE(result.is_response());
                           });
  RunLoopUntilIdle();

  // Register the LogEngine.
  LogEngine log_engine(std::move(startup_context_));
  fidl::BindingSet<fuchsia::accessibility::tts::Engine> log_engine_bindings;
  auto engine_handle = log_engine_bindings.AddBinding(&log_engine);
  tts_manager_->RegisterEngine(
      std::move(engine_handle),
      [](fuchsia::accessibility::tts::EngineRegistry_RegisterEngine_Result result) {
        EXPECT_TRUE(result.is_response());
      });
  RunLoopUntilIdle();

  fuchsia::accessibility::tts::Utterance utterance;
  utterance.set_message("hello world");
  speaker->Enqueue(std::move(utterance), [](auto) {});
  RunLoopUntilIdle();
  EXPECT_TRUE(LogContains("Received utterance: hello world"));

  speaker->Speak([](auto) {});
  RunLoopUntilIdle();
  EXPECT_TRUE(LogContains("Received a Speak. Dispatching the following utterances:"));
  EXPECT_TRUE(LogContains("  - hello world"));

  speaker->Cancel([]() {});
  RunLoopUntilIdle();
  EXPECT_TRUE(LogContains("Received a Cancel"));

  // Print the logs out for debugging.
  std::cerr << log_output_.str();
}

}  // namespace
}  // namespace a11y
