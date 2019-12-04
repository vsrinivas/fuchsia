// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>

#include <queue>

#include "lib/media/cpp/timeline_function.h"
#include "lib/media/cpp/type_converters.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/media/playback/mediaplayer/test/command_queue.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_audio.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_scenic.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_wav_reader.h"
#include "src/media/playback/mediaplayer/test/sink_feeder.h"

namespace media_player {
namespace test {

static constexpr uint16_t kSamplesPerFrame = 2;      // Stereo
static constexpr uint32_t kFramesPerSecond = 48000;  // 48kHz
static constexpr size_t kSinkFeedSize = 65536;
static constexpr uint32_t kSinkFeedMaxPacketSize = 4096;
static constexpr uint32_t kSinkFeedMaxPacketCount = 10;

constexpr char kBearFilePath[] = "/pkg/data/media_test_data/bear.mp4";

// Base class for mediaplayer tests.
class MediaPlayerTests : public sys::testing::TestWithEnvironment {
 protected:
  void SetUp() override {
    auto services = CreateServices();

    // Add the service under test using its launch info.
    fuchsia::sys::LaunchInfo launch_info{
        "fuchsia-pkg://fuchsia.com/mediaplayer#meta/mediaplayer.cmx"};
    zx_status_t status = services->AddServiceWithLaunchInfo(
        std::move(launch_info), fuchsia::media::playback::Player::Name_);
    EXPECT_EQ(ZX_OK, status);

    services->AddService(fake_audio_.GetRequestHandler());
    services->AddService(fake_scenic_.GetRequestHandler());

    // Create the synthetic environment.
    environment_ = CreateNewEnclosingEnvironment("mediaplayer_tests", std::move(services));

    // Instantiate the player under test.
    environment_->ConnectToService(player_.NewRequest());

    commands_.Init(player_.get());

    player_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(ERROR) << "Player connection closed, status " << status << ".";
      player_connection_closed_ = true;
      QuitLoop();
    });

    player_.events().OnStatusChanged = [this](fuchsia::media::playback::PlayerStatus status) {
      commands_.NotifyStatusChanged(status);
    };
  }

  void TearDown() override { EXPECT_FALSE(player_connection_closed_); }

  // Queues commands to wait for end of stream and to call |QuitLoop|.
  void QuitOnEndOfStream() {
    commands_.WaitForEndOfStream();
    commands_.Invoke([this]() { QuitLoop(); });
  }

  // Executes queued commands
  void Execute() {
    commands_.Execute();
    RunLoop();
  }

  // Creates a view.
  void CreateView() {
    auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();
    player_->CreateView(std::move(view_token));
    view_holder_token_ = std::move(view_holder_token);
  }

  fuchsia::media::playback::PlayerPtr player_;
  bool player_connection_closed_ = false;

  FakeWavReader fake_reader_;
  FakeAudio fake_audio_;
  FakeScenic fake_scenic_;
  fuchsia::ui::views::ViewHolderToken view_holder_token_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  bool sink_connection_closed_ = false;
  SinkFeeder sink_feeder_;
  CommandQueue commands_;
};

// Play a synthetic WAV file from beginning to end.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_PlayWav) {
  fake_audio_.renderer().ExpectPackets({{0, 4096, 0x20c39d1e31991800},
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

  fuchsia::media::playback::SeekingReaderPtr fake_reader_ptr;
  fidl::InterfaceRequest<fuchsia::media::playback::SeekingReader> reader_request =
      fake_reader_ptr.NewRequest();
  fake_reader_.Bind(std::move(reader_request));

  fuchsia::media::playback::SourcePtr source;
  player_->CreateReaderSource(std::move(fake_reader_ptr), source.NewRequest());
  player_->SetSource(std::move(source));

  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
}

// Play a synthetic WAV file from beginning to end, delaying the retirement of
// the last packet to simulate delayed end-of-stream recognition.
// TODO(fxb/35616): Flaking.
TEST_F(MediaPlayerTests, DISABLED_PlayWavDelayEos) {
  fake_audio_.renderer().ExpectPackets({{0, 4096, 0x20c39d1e31991800},
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

  fuchsia::media::playback::SeekingReaderPtr fake_reader_ptr;
  fidl::InterfaceRequest<fuchsia::media::playback::SeekingReader> reader_request =
      fake_reader_ptr.NewRequest();
  fake_reader_.Bind(std::move(reader_request));

  fuchsia::media::playback::SourcePtr source;
  player_->CreateReaderSource(std::move(fake_reader_ptr), source.NewRequest());
  player_->SetSource(std::move(source));

  fake_audio_.renderer().DelayPacketRetirement(15360);

  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
}

// Play a synthetic WAV file from beginning to end, retaining packets. This
// tests the ability of the player to handle the case in which the audio
// renderer is holding on to packets for too long.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_PlayWavRetainPackets) {
  fake_audio_.renderer().SetRetainPackets(true);

  fuchsia::media::playback::SeekingReaderPtr fake_reader_ptr;
  fidl::InterfaceRequest<fuchsia::media::playback::SeekingReader> reader_request =
      fake_reader_ptr.NewRequest();
  fake_reader_.Bind(std::move(reader_request));

  // Need more than 1s of data.
  fake_reader_.SetSize(256000);

  fuchsia::media::playback::SourcePtr source;
  player_->CreateReaderSource(std::move(fake_reader_ptr), source.NewRequest());
  player_->SetSource(std::move(source));

  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
}

// Play an LPCM elementary stream using |ElementarySource|
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_ElementarySource) {
  fake_audio_.renderer().ExpectPackets({{0, 4096, 0xd2fbd957e3bf0000},
                                        {1024, 4096, 0xda25db3fa3bf0000},
                                        {2048, 4096, 0xe227e0f6e3bf0000},
                                        {3072, 4096, 0xe951e2dea3bf0000},
                                        {4096, 4096, 0x37ebf7d3e3bf0000},
                                        {5120, 4096, 0x3f15f9bba3bf0000},
                                        {6144, 4096, 0x4717ff72e3bf0000},
                                        {7168, 4096, 0x4e42015aa3bf0000},
                                        {8192, 4096, 0xeabc5347e3bf0000},
                                        {9216, 4096, 0xf1e6552fa3bf0000},
                                        {10240, 4096, 0xf9e85ae6e3bf0000},
                                        {11264, 4096, 0x01125ccea3bf0000},
                                        {12288, 4096, 0x4fac71c3e3bf0000},
                                        {13312, 4096, 0x56d673aba3bf0000},
                                        {14336, 4096, 0x5ed87962e3bf0000},
                                        {15360, 4096, 0x66027b4aa3bf0000}});

  fuchsia::media::playback::ElementarySourcePtr elementary_source;
  player_->CreateElementarySource(0, false, false, nullptr, elementary_source.NewRequest());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  audio_stream_type.channels = kSamplesPerFrame;
  audio_stream_type.frames_per_second = kFramesPerSecond;
  fuchsia::media::StreamType stream_type;
  stream_type.medium_specific.set_audio(std::move(audio_stream_type));
  stream_type.encoding = fuchsia::media::AUDIO_ENCODING_LPCM;

  fuchsia::media::SimpleStreamSinkPtr sink;
  elementary_source->AddStream(std::move(stream_type), kFramesPerSecond, 1, sink.NewRequest());
  sink.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "SimpleStreamSink connection closed.";
    sink_connection_closed_ = true;
    QuitLoop();
  });

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::media::playback::ElementarySource>| to a
  // |fidl::InterfaceHandle<fuchsia::media::playback::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(dalesat): Do this safely once FIDL-329 is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source>(
      elementary_source.Unbind().TakeChannel()));

  sink_feeder_.Init(std::move(sink), kSinkFeedSize, kSamplesPerFrame * sizeof(int16_t),
                    kSinkFeedMaxPacketSize, kSinkFeedMaxPacketCount);

  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_FALSE(sink_connection_closed_);
}

// Opens an SBC elementary stream using |ElementarySource|.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_ElementarySourceWithSBC) {
  fuchsia::media::playback::ElementarySourcePtr elementary_source;
  player_->CreateElementarySource(1, false, false, nullptr, elementary_source.NewRequest());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  audio_stream_type.channels = kSamplesPerFrame;
  audio_stream_type.frames_per_second = kFramesPerSecond;
  fuchsia::media::StreamType stream_type;
  stream_type.medium_specific.set_audio(std::move(audio_stream_type));
  stream_type.encoding = fuchsia::media::AUDIO_ENCODING_SBC;

  fuchsia::media::SimpleStreamSinkPtr sink;
  elementary_source->AddStream(std::move(stream_type), kFramesPerSecond, 1, sink.NewRequest());
  sink.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "SimpleStreamSink connection closed.";
    sink_connection_closed_ = true;
    QuitLoop();
  });

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::media::playback::ElementarySource>| to a
  // |fidl::InterfaceHandle<fuchsia::media::playback::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(FIDL-329): Do this safely once FIDL-329 is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source>(
      elementary_source.Unbind().TakeChannel()));

  commands_.WaitForAudioConnected();
  commands_.Invoke([this]() { QuitLoop(); });

  Execute();
  EXPECT_FALSE(sink_connection_closed_);
}

// Opens an AAC elementary stream using |ElementarySource|.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_ElementarySourceWithAAC) {
  fuchsia::media::playback::ElementarySourcePtr elementary_source;
  player_->CreateElementarySource(1, false, false, nullptr, elementary_source.NewRequest());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  audio_stream_type.channels = kSamplesPerFrame;
  audio_stream_type.frames_per_second = kFramesPerSecond;
  fuchsia::media::StreamType stream_type;
  stream_type.medium_specific.set_audio(std::move(audio_stream_type));
  stream_type.encoding = fuchsia::media::AUDIO_ENCODING_AAC;

  fuchsia::media::SimpleStreamSinkPtr sink;
  elementary_source->AddStream(std::move(stream_type), kFramesPerSecond, 1, sink.NewRequest());
  sink.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "SimpleStreamSink connection closed.";
    sink_connection_closed_ = true;
    QuitLoop();
  });

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::media::playback::ElementarySource>| to a
  // |fidl::InterfaceHandle<fuchsia::media::playback::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(FIDL-329): Do this safely once FIDL-329 is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source>(
      elementary_source.Unbind().TakeChannel()));

  commands_.WaitForAudioConnected();
  commands_.Invoke([this]() { QuitLoop(); });

  Execute();
  EXPECT_FALSE(sink_connection_closed_);
}

// Opens an AACLATM elementary stream using |ElementarySource|.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_ElementarySourceWithAACLATM) {
  fuchsia::media::playback::ElementarySourcePtr elementary_source;
  player_->CreateElementarySource(1, false, false, nullptr, elementary_source.NewRequest());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  audio_stream_type.channels = kSamplesPerFrame;
  audio_stream_type.frames_per_second = kFramesPerSecond;
  fuchsia::media::StreamType stream_type;
  stream_type.medium_specific.set_audio(std::move(audio_stream_type));
  stream_type.encoding = fuchsia::media::AUDIO_ENCODING_AACLATM;

  fuchsia::media::SimpleStreamSinkPtr sink;
  elementary_source->AddStream(std::move(stream_type), kFramesPerSecond, 1, sink.NewRequest());
  sink.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "SimpleStreamSink connection closed.";
    sink_connection_closed_ = true;
    QuitLoop();
  });

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::media::playback::ElementarySource>| to a
  // |fidl::InterfaceHandle<fuchsia::media::playback::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(FIDL-329): Do this safely once FIDL-329 is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source>(
      elementary_source.Unbind().TakeChannel()));

  commands_.WaitForAudioConnected();
  commands_.Invoke([this]() { QuitLoop(); });

  Execute();
  EXPECT_FALSE(sink_connection_closed_);
}

// Tries to open a bogus elementary stream using |ElementarySource|.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_ElementarySourceWithBogus) {
  fuchsia::media::playback::ElementarySourcePtr elementary_source;
  player_->CreateElementarySource(1, false, false, nullptr, elementary_source.NewRequest());

  fuchsia::media::AudioStreamType audio_stream_type;
  audio_stream_type.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
  audio_stream_type.channels = kSamplesPerFrame;
  audio_stream_type.frames_per_second = kFramesPerSecond;
  fuchsia::media::StreamType stream_type;
  stream_type.medium_specific.set_audio(std::move(audio_stream_type));
  stream_type.encoding = "bogus encoding";

  fuchsia::media::SimpleStreamSinkPtr sink;
  elementary_source->AddStream(std::move(stream_type), kFramesPerSecond, 1, sink.NewRequest());
  sink.set_error_handler([this](zx_status_t status) {
    FX_LOGS(ERROR) << "SimpleStreamSink connection closed.";
    sink_connection_closed_ = true;
    QuitLoop();
  });

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::media::playback::ElementarySource>| to a
  // |fidl::InterfaceHandle<fuchsia::media::playback::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(FIDL-329): Do this safely once is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source>(
      elementary_source.Unbind().TakeChannel()));

  commands_.WaitForProblem();
  commands_.Invoke([this]() { QuitLoop(); });

  Execute();
  EXPECT_FALSE(sink_connection_closed_);
}

// Play a real A/V file from beginning to end.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_PlayBear) {
  // TODO(dalesat): Use ExpectPackets for audio.
  // This doesn't currently work, because the decoder behaves differently on
  // different targets.

  fake_scenic_.session().SetExpectations(
      1,
      {
          .width = 2,
          .height = 2,
          .stride = 2 * sizeof(uint32_t),
          .pixel_format = fuchsia::images::PixelFormat::BGRA_8,
      },
      {
          .width = 1280,
          .height = 738,
          .stride = 1280,
          .pixel_format = fuchsia::images::PixelFormat::YV12,
      },
      720, {{0, 944640, 0x0864378c3655ba47},          {133729451, 944640, 0x2481a21b1e543c8e},
            {167096118, 944640, 0xe4294049f22539bc},  {200462784, 944640, 0xde1058aba916ffad},
            {233829451, 944640, 0xc3fc580b34dc0383},  {267196118, 944640, 0xff31322e5ccdebe0},
            {300562784, 944640, 0x64d31206ece7417f},  {333929451, 944640, 0xf1c6bf7fe1be29be},
            {367296118, 944640, 0x72f44e5249a05c15},  {400662784, 944640, 0x1ad7e92183fb3aa4},
            {434029451, 944640, 0x24b78b95d8c8b73d},  {467396118, 944640, 0x25a798d9af5a1b7e},
            {500762784, 944640, 0x3379288b1f4197a5},  {534129451, 944640, 0x15fb9c205590cbc9},
            {567496118, 944640, 0xc04a1834aec8b399},  {600862784, 944640, 0x97eded0e3b6348d3},
            {634229451, 944640, 0x09dba227982ba479},  {667596118, 944640, 0x4d2a1042babc479c},
            {700962784, 944640, 0x379f96a35774dc2b},  {734329451, 944640, 0x2d95a4b5506bd4c3},
            {767696118, 944640, 0xda99bf00cd971999},  {801062784, 944640, 0x20a21550eb717da2},
            {834429451, 944640, 0x3733b96d2279460b},  {867796118, 944640, 0x8ea51ee0088cda67},
            {901162784, 944640, 0x8d6af19e5d9629ae},  {934529451, 944640, 0xd9765bd28098f093},
            {967896118, 944640, 0x9a747455b496c9d1},  {1001262784, 944640, 0xfc8e90e73cc086f6},
            {1034629451, 944640, 0xc3dec92946fc0005}, {1067996118, 944640, 0x215b196e790214c4},
            {1101362784, 944640, 0x30b114015d719041}, {1134729451, 944640, 0x5ed6e582ac4022a1},
            {1168096118, 944640, 0xbccb6f8ba8601507}, {1201462784, 944640, 0x34eab6666dc6c717},
            {1234829451, 944640, 0x5e33bfc44650245f}, {1268196118, 944640, 0x736397b78e0850ff},
            {1301562784, 944640, 0x620d7190a9e49a31}, {1334929451, 944640, 0x436e952327e311ea},
            {1368296118, 944640, 0xf6fa16fc170a85f3}, {1401662784, 944640, 0x9f457e1a66323ead},
            {1435029451, 944640, 0xb1747e31ea5358db}, {1468396118, 944640, 0x4da84ec1c5cb45de},
            {1501762784, 944640, 0x5454f9007dc4de01}, {1535129451, 944640, 0x8e9777accf38e4f0},
            {1568496118, 944640, 0x16a2ebade809e497}, {1601862784, 944640, 0x36d323606ebca2f4},
            {1635229451, 944640, 0x17eaf1e84353dec9}, {1668596118, 944640, 0xdb1b344498520386},
            {1701962784, 944640, 0xec53764065860e7f}, {1735329451, 944640, 0x110a7dddd4c45a54},
            {1768696118, 944640, 0x6df1c973722f01c7}, {1802062784, 944640, 0x2e18f1e1544e002a},
            {1835429451, 944640, 0x0de7b784dd8b0494}, {1868796118, 944640, 0x6e254cd1652be6a9},
            {1902162784, 944640, 0x6353cb7c270b06c2}, {1935529451, 944640, 0x8d62a2ddb0350ab9},
            {1968896118, 944640, 0xaf0ee1376ded95cd}, {2002262784, 944640, 0xf617917814de4169},
            {2035629451, 944640, 0xf686efcec861909f}, {2068996118, 944640, 0x539f93afe6863cca},
            {2102362784, 944640, 0x12c5c5e4eb5b2649}, {2135729451, 944640, 0x984cf8179effd823},
            {2169096118, 944640, 0xfcb0cc2eb449ed16}, {2202462784, 944640, 0xf070b3572db477cc},
            {2235829451, 944640, 0x5dd53f712ce8e1a6}, {2269196118, 944640, 0x02e0600528534bef},
            {2302562784, 944640, 0x53120fbaca19e13b}, {2335929451, 944640, 0xd66e3cb3e70897eb},
            {2369296118, 944640, 0x9f4138aa8e84cbf4}, {2402662784, 944640, 0xf350694d6a12ec39},
            {2436029451, 944640, 0x08c986a97ab8fbb3}, {2469396118, 944640, 0x229d2b908659b728},
            {2502762784, 944640, 0xf54cbe4582a3f8e1}, {2536129451, 944640, 0x8c8985c6649a3e1c},
            {2569496118, 944640, 0x711e04eccc5e4527}, {2602862784, 944640, 0x78e2979034921e70},
            {2636229451, 944640, 0x51c3524f5bf83a62}, {2669596118, 944640, 0x12b6f7b7591e7044},
            {2702962784, 944640, 0xca8d7ac09b973a4b}, {2736329451, 944640, 0x3e666b376fcaa466},
            {2769696118, 944640, 0x8f3657c9648b6dbb}, {2803062784, 944640, 0x19a30916a3375f4e}});

  CreateView();
  commands_.SetFile(kBearFilePath);
  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
}

// Play a real A/V file from beginning to end, retaining audio packets. This
// tests the ability of the player to handle the case in which the audio
// renderer is holding on to packets for too long.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_PlayBearRetainAudioPackets) {
  CreateView();
  fake_audio_.renderer().SetRetainPackets(true);

  commands_.SetFile(kBearFilePath);
  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
}

// Regression test for US-544.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_RegressionTestUS544) {
  CreateView();
  commands_.SetFile(kBearFilePath);

  // Play for two seconds and pause.
  commands_.Play();
  commands_.WaitForPosition(zx::sec(2));
  commands_.Pause();

  // Wait a bit.
  commands_.Sleep(zx::sec(2));

  // Seek to the beginning and resume playing.
  commands_.Seek(zx::sec(0));
  commands_.Play();

  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
}

// Regression test for QA-539.
// Verifies that the player can play two files in a row.
// TODO(fxb/42050): This test sometimes times out on CQ/CI bots due to the support for the FIDL v1
// wire-format transition. We should re-enable this test after the FIDL v1 transition is complete.
TEST_F(MediaPlayerTests, DISABLED_RegressionTestQA539) {
  CreateView();
  commands_.SetFile(kBearFilePath);

  // Play the file to the end.
  commands_.Play();
  commands_.WaitForEndOfStream();

  // Reload the file.
  commands_.SetFile(kBearFilePath);

  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
}

}  // namespace test
}  // namespace media_player
