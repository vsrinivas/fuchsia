// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/connect.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/services/media_service.fidl.h"
#include "apps/media/src/fidl/fidl_formatting.h"
#include "apps/media/src/media_service/test/fake_renderer.h"
#include "apps/media/src/media_service/test/fake_wav_reader.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace media {
namespace test {

class MediaPlayerTester {
 public:
  MediaPlayerTester()
      : application_context_(
            app::ApplicationContext::CreateFromStartupInfo()) {
    FTL_LOG(INFO) << "MediaPlayerTest starting";

    FTL_LOG(INFO) << "connecting to MediaService";
    MediaServicePtr media_service =
        application_context_->ConnectToEnvironmentService<MediaService>();
    FTL_LOG(INFO) << "connected to MediaService "
                  << (media_service ? "ok" : "NULL PTR");

    fake_renderer_.SetPtsRate(TimelineRate(48000, 1));

    fake_renderer_.ExpectPackets({{0, false, 4096, 0x20c39d1e31991800},
                                  {1024, false, 4096, 0xeaf137125d313800},
                                  {2048, false, 4096, 0x6162095671991800},
                                  {3072, false, 4096, 0x36e551c7dd41f800},
                                  {4096, false, 4096, 0x23dcbf6fb1991800},
                                  {5120, false, 4096, 0xee0a5963dd313800},
                                  {6144, false, 4096, 0x647b2ba7f1991800},
                                  {7168, false, 4096, 0x39fe74195d41f800},
                                  {8192, false, 4096, 0xb3de76b931991800},
                                  {9216, false, 4096, 0x7e0c10ad5d313800},
                                  {10240, false, 4096, 0xf47ce2f171991800},
                                  {11264, false, 4096, 0xca002b62dd41f800},
                                  {12288, false, 4096, 0xb6f7990ab1991800},
                                  {13312, false, 4096, 0x812532fedd313800},
                                  {14336, false, 4096, 0xf7960542f1991800},
                                  {15360, false, 4052, 0x7308a9824acbd5ea},
                                  {16373, true, 0, 0x0000000000000000}});

    SeekingReaderPtr fake_reader_ptr;
    fidl::InterfaceRequest<SeekingReader> reader_request =
        fake_reader_ptr.NewRequest();
    fake_reader_.Bind(std::move(reader_request));

    MediaRendererPtr fake_renderer_ptr;
    fidl::InterfaceRequest<MediaRenderer> renderer_request =
        fake_renderer_ptr.NewRequest();
    fake_renderer_.Bind(std::move(renderer_request));

    FTL_LOG(INFO) << "creating player";
    media_service->CreatePlayer(std::move(fake_reader_ptr),
                                std::move(fake_renderer_ptr), nullptr,
                                media_player_.NewRequest());
    FTL_LOG(INFO) << "player created " << (media_player_ ? "ok" : "NULL PTR");

    HandleStatusUpdates();
    FTL_LOG(INFO) << "calling play";
    media_player_->Play();
    FTL_LOG(INFO) << "called play";
  }

 private:
  void HandleStatusUpdates(uint64_t version = MediaPlayer::kInitialStatus,
                           MediaPlayerStatusPtr status = nullptr) {
    if (status) {
      if (status->end_of_stream) {
        ended_ = true;
        FTL_LOG(INFO) << "MediaPlayerTest "
                      << (fake_renderer_.expected() ? "SUCCEEDED" : "FAILED");
        mtl::MessageLoop::GetCurrent()->PostQuitTask();
      }
    }

    // Request a status update.
    media_player_->GetStatus(
        version, [this](uint64_t version, MediaPlayerStatusPtr status) {
          HandleStatusUpdates(version, std::move(status));
        });
  }

  std::unique_ptr<app::ApplicationContext> application_context_;
  FakeWavReader fake_reader_;
  FakeRenderer fake_renderer_;
  MediaPlayerPtr media_player_;
  bool ended_ = false;
};

}  // namespace test
}  // namespace media

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;

  media::test::MediaPlayerTester app;

  loop.Run();
  return 0;
}
