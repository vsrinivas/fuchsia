// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <tts/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"

namespace {

class TtsClient {
 public:
  TtsClient(fxl::Closure quit_callback);
  void Say(std::string words);

 private:
  fxl::Closure quit_callback_;
  tts::TtsServicePtr tts_service_;
};

TtsClient::TtsClient(fxl::Closure quit_callback)
    : quit_callback_(quit_callback) {
  FXL_DCHECK(quit_callback_);
  auto app_ctx = component::ApplicationContext::CreateFromStartupInfo();
  tts_service_ = app_ctx->ConnectToEnvironmentService<tts::TtsService>();
  tts_service_.set_error_handler([this]() {
    printf("Connection error when trying to talk to the TtsService\n");
    quit_callback_();
  });
}

void TtsClient::Say(std::string words) {
  tts_service_->Say(words, 0, [this](uint64_t token) { quit_callback_(); });
}

}  // namespace

int main(int argc, const char** argv) {
  if (argc < 2) {
    printf("usage: %s [words to speak]\n", argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  TtsClient client(
      [&loop]() { async::PostTask(loop.async(), [&loop]() { loop.Quit(); }); });

  std::string words(argv[1]);
  for (int i = 2; i < argc; ++i) {
    words += " ";
    words += argv[i];
  }

  async::PostTask(loop.async(), [&]() { client.Say(std::move(words)); });

  loop.Run();

  return 0;
}
