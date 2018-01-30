// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/media/fidl/tts_service.fidl.h"

namespace {

class TtsClient {
 public:
  TtsClient();
  void Say(std::string words);

 private:
  media::TtsServicePtr tts_service_;
};

TtsClient::TtsClient() {
  auto app_ctx = app::ApplicationContext::CreateFromStartupInfo();
  tts_service_ = app_ctx->ConnectToEnvironmentService<media::TtsService>();
  tts_service_.set_error_handler([]() {
    printf("Connection error when trying to talk to the TtsService\n");
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });
}

void TtsClient::Say(std::string words) {
  tts_service_->Say(words, 0, [](uint64_t token) {
    fsl::MessageLoop::GetCurrent()->PostQuitTask();
  });
}

}  // namespace

int main(int argc, const char** argv) {
  if (argc < 2) {
    printf("usage: %s [words to speak]\n", argv[0]);
    return -1;
  }

  fsl::MessageLoop loop;
  TtsClient client;
  std::string words(argv[1]);
  for (int i = 2; i < argc; ++i) {
    words += " ";
    words += argv[i];
  }

  loop.task_runner()->PostTask([&]() { client.Say(std::move(words)); });

  loop.Run();

  return 0;
}
