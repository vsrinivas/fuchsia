// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/app/cpp/application_context.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/tts_service.fidl.h"

namespace media {
namespace tts {

class TtsSpeaker;

class TtsServiceImpl {
 public:
  TtsServiceImpl(std::unique_ptr<app::ApplicationContext> application_context);
  ~TtsServiceImpl();

  zx_status_t Init();

 private:
  class Client : public TtsService {
   public:
    Client(TtsServiceImpl* owner, fidl::InterfaceRequest<TtsService> request);
    ~Client();

    void Shutdown();

    // TtsService
    void Say(const fidl::String& words,
             uint64_t token,
             const SayCallback& cbk) override;

   private:
    void OnSpeakComplete(std::shared_ptr<TtsSpeaker> speaker,
                         uint64_t token,
                         SayCallback cbk);

    TtsServiceImpl* const owner_;
    fidl::Binding<TtsService> binding_;
    std::set<std::shared_ptr<TtsSpeaker>> active_speakers_;
  };

  friend class Client;

  std::unique_ptr<app::ApplicationContext> application_context_;
  std::set<Client*> clients_;
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TtsServiceImpl);
};

}  // namespace tts
}  // namespace media
