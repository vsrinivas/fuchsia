// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TTS_TTS_SERVICE_IMPL_H_
#define GARNET_BIN_TTS_TTS_SERVICE_IMPL_H_

#include <memory>
#include <set>

#include <fuchsia/tts/cpp/fidl.h>

#include "lib/component/cpp/startup_context.h"

namespace tts {

class TtsSpeaker;

class TtsServiceImpl {
 public:
  TtsServiceImpl(std::unique_ptr<component::StartupContext> startup_context);
  ~TtsServiceImpl();

  zx_status_t Init();

 private:
  class Client : public fuchsia::tts::TtsService {
   public:
    Client(TtsServiceImpl* owner,
           fidl::InterfaceRequest<fuchsia::tts::TtsService> request);
    ~Client();

    void Shutdown();

    // TtsService
    void Say(fidl::StringPtr words, uint64_t token, SayCallback cbk) override;

   private:
    void OnSpeakComplete(std::shared_ptr<TtsSpeaker> speaker, uint64_t token,
                         SayCallback cbk);

    TtsServiceImpl* const owner_;
    fidl::Binding<TtsService> binding_;
    std::set<std::shared_ptr<TtsSpeaker>> active_speakers_;
  };

  friend class Client;

  std::unique_ptr<component::StartupContext> startup_context_;
  std::set<Client*> clients_;
  async_dispatcher_t* dispatcher_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TtsServiceImpl);
};

}  // namespace tts

#endif  // GARNET_BIN_TTS_TTS_SERVICE_IMPL_H_
