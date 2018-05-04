// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/media.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/media/media_player/fidl/fidl_formatting.h"
#include "garnet/bin/media/media_player/test/fake_audio_renderer.h"
#include "garnet/bin/media/media_player/test/fake_wav_reader.h"
#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media_player {
namespace test {

class MediaPlayerTester {
 public:
  MediaPlayerTester(fxl::Closure quit_callback)
      : application_context_(
            component::ApplicationContext::CreateFromStartupInfo()),
        quit_callback_(quit_callback) {
    FXL_DCHECK(quit_callback_);
    FXL_LOG(INFO) << "MediaPlayerTest starting";

    FXL_LOG(INFO) << "creating player";
    media_player_ =
        application_context_->ConnectToEnvironmentService<MediaPlayer>();

    fake_audio_renderer_.SetPtsUnits(48000, 1);

    fake_audio_renderer_.ExpectPackets({{0, 4096, 0x20c39d1e31991800},
                                        {1024, 4096, 0xeaf137125d313800},
                                        {2048, 4096, 0x6162095671991800},
                                        {3072, 4096, 0x36e551c7dd41f800},
                                        {4096, 4096, 0x23dcbf6fb1991800},
                                        {5120, 4096, 0xee0a5963dd313800},
                                        {6144, 4096, 0x647b2ba7f1991800},
                                        {7168, 4096, 0x39fe74195d41f800},
                                        {8192, 4096, 0xb3de76b931991800},
                                        {9216, 4096, 0x7e0c10ad5d313800},
                                        {10240, 4096, 0xf47ce2f171991800},
                                        {11264, 4096, 0xca002b62dd41f800},
                                        {12288, 4096, 0xb6f7990ab1991800},
                                        {13312, 4096, 0x812532fedd313800},
                                        {14336, 4096, 0xf7960542f1991800},
                                        {15360, 4052, 0x7308a9824acbd5ea}});

    SeekingReaderPtr fake_reader_ptr;
    fidl::InterfaceRequest<SeekingReader> reader_request =
        fake_reader_ptr.NewRequest();
    fake_reader_.Bind(std::move(reader_request));

    media::AudioRenderer2Ptr fake_audio_renderer_ptr;
    fake_audio_renderer_.Bind(fake_audio_renderer_ptr.NewRequest());

    media_player_->SetAudioRenderer(std::move(fake_audio_renderer_ptr));

    media_player_->SetReaderSource(std::move(fake_reader_ptr));
    FXL_LOG(INFO) << "player created " << (media_player_ ? "ok" : "NULL PTR");

    HandleStatusUpdates();
    FXL_LOG(INFO) << "calling play";
    media_player_->Play();
    FXL_LOG(INFO) << "called play";
  }

 private:
  void HandleStatusUpdates(uint64_t version = media::kInitialStatus,
                           MediaPlayerStatusPtr status = nullptr) {
    if (status) {
      if (status->end_of_stream) {
        ended_ = true;
        FXL_LOG(INFO) << "MediaPlayerTest "
                      << (fake_audio_renderer_.expected() ? "SUCCEEDED"
                                                          : "FAILED");
        quit_callback_();
      }
    }

    // Request a status update.
    media_player_->GetStatus(
        version, [this](uint64_t version, MediaPlayerStatus status) {
          HandleStatusUpdates(version, fidl::MakeOptional(std::move(status)));
        });
  }

  std::unique_ptr<component::ApplicationContext> application_context_;
  fxl::Closure quit_callback_;
  FakeWavReader fake_reader_;
  FakeAudioRenderer fake_audio_renderer_;
  MediaPlayerPtr media_player_;
  bool ended_ = false;
};

}  // namespace test
}  // namespace media_player

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);

  media_player::test::MediaPlayerTester app(
      [&loop]() { async::PostTask(loop.async(), [&loop]() { loop.Quit(); }); });

  loop.Run();
  return 0;
}
