// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/bin/a11y_tts_log_engine/log_engine.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/wire_format.h>

#include <memory>
#include <vector>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

__BEGIN_CDECLS
// This does not come from header file as this function should only be used in
// tests and is not for general use.
void fx_log_reset_global_for_testing(void);
__END_CDECLS

namespace a11y {
namespace {

class LogEngineTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    startup_context_ = sys::ComponentContext::Create();
    tts_manager_ = std::make_unique<TtsManager>(startup_context_.get());
    fx_log_reset_global_for_testing();
    ASSERT_EQ(ZX_OK, InitLogger());
  }

  // Initializes the global logger.
  //
  // This redirects the output to the socket pair |log_socket_local_| and |log_socket_remote_|
  // so that we can capture it.
  zx_status_t InitLogger() {
    EXPECT_EQ(ZX_OK,
              zx::socket::create(ZX_SOCKET_DATAGRAM, &log_socket_local_, &log_socket_remote_));

    fx_logger_config_t config = {.min_severity = FX_LOG_INFO,
                                 .console_fd = -1,
                                 .log_service_channel = log_socket_remote_.release(),
                                 .tags = nullptr,
                                 .num_tags = 0};

    return fx_log_init_with_config(&config);
  }

  // Reads log packets from |log_socket_local_| and outputs each message to |log_output_|.
  //
  // Returns ZX_OK once there are no more packets left to read.
  zx_status_t ConsumeLogMessages() {
    fx_log_packet_t packet;
    zx_status_t result;

    while ((result = log_socket_local_.read(0, &packet, sizeof(packet), nullptr)) == ZX_OK) {
      // Write the log message to log_output_. The packet.data buffer contains tags and the message
      // separated with a null byte. InitLogger does not set any tags, so this just skips over
      // the the separator.
      log_output_ << (packet.data + 1) << std::endl;
    }

    // It's OK if there are no more log packets to read.
    if (result == ZX_ERR_SHOULD_WAIT) {
      return ZX_OK;
    }

    return result;
  }

  // Returns true if |log_output_| contains |log_message|.
  bool LogContains(const std::string& log_message) {
    EXPECT_EQ(ZX_OK, ConsumeLogMessages());
    return log_output_.str().find(log_message) != std::string::npos;
  }

  std::unique_ptr<sys::ComponentContext> startup_context_;
  std::unique_ptr<TtsManager> tts_manager_;
  std::stringstream log_output_;
  zx::socket log_socket_local_, log_socket_remote_;
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
