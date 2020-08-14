// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/media/playback/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <lib/syslog/cpp/macros.h>

#include <queue>

#include "lib/media/cpp/timeline_function.h"
#include "lib/media/cpp/type_converters.h"
#include "lib/ui/scenic/cpp/view_token_pair.h"
#include "src/lib/fsl/io/fd.h"
#include "src/media/playback/mediaplayer/test/command_queue.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_audio.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_scenic.h"
#include "src/media/playback/mediaplayer/test/fakes/fake_sysmem.h"
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
constexpr char kOpusFilePath[] = "/pkg/data/media_test_data/sfx-opus-441.webm";

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
    services->AddService(fake_sysmem_.GetRequestHandler());

    fake_scenic_.SetSysmemAllocator(&fake_sysmem_);

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

  zx::vmo CreateVmo(size_t size) {
    zx::vmo result;
    zx_status_t status = zx::vmo::create(size, 0, &result);
    FX_CHECK(status == ZX_OK);
    return result;
  }

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

  std::list<std::unique_ptr<FakeSysmem::Expectations>> BlackImageSysmemExpectations();

  std::list<std::unique_ptr<FakeSysmem::Expectations>> BearVideoImageSysmemExpectations();

  std::list<std::unique_ptr<FakeSysmem::Expectations>> BearSysmemExpectations();

  fuchsia::media::playback::PlayerPtr player_;
  bool player_connection_closed_ = false;

  FakeWavReader fake_reader_;
  FakeAudio fake_audio_;
  FakeScenic fake_scenic_;
  FakeSysmem fake_sysmem_;
  fuchsia::ui::views::ViewHolderToken view_holder_token_;
  std::unique_ptr<sys::testing::EnclosingEnvironment> environment_;
  bool sink_connection_closed_ = false;
  SinkFeeder sink_feeder_;
  CommandQueue commands_;
};

std::list<std::unique_ptr<FakeSysmem::Expectations>>
MediaPlayerTests::BlackImageSysmemExpectations() {
  std::list<std::unique_ptr<FakeSysmem::Expectations>> result;
  result
      .push_back(std::
                     make_unique<FakeSysmem::Expectations>(
                         FakeSysmem::Expectations{

                             .constraints_ = {fuchsia::sysmem::BufferCollectionConstraints{
                                                  .usage =
                                                      {
                                                          .cpu =
                                                              fuchsia::sysmem::cpuUsageRead |
                                                              fuchsia::sysmem::cpuUsageReadOften |
                                                              fuchsia::sysmem::cpuUsageWrite |
                                                              fuchsia::sysmem::cpuUsageWriteOften,
                                                      },
                                                  .min_buffer_count = 1,
                                                  .has_buffer_memory_constraints = true,
                                                  .buffer_memory_constraints =
                                                      {
                                                          .min_size_bytes = 16,
                                                          .ram_domain_supported = true,
                                                      },
                                                  .image_format_constraints_count = 1,
                                                  .image_format_constraints =
                                                      {
                                                          fuchsia::sysmem::
                                                              ImageFormatConstraints{
                                                                  .pixel_format =
                                                                      {
                                                                          .type = fuchsia::
                                                                              sysmem::PixelFormatType::R8G8B8A8},
                                                                  .color_spaces_count = 1,
                                                                  .color_space =
                                                                      {
                                                                          fuchsia::sysmem::
                                                                              ColorSpace{.type =
                                                                                             fuchsia::
                                                                                                 sysmem::ColorSpaceType::SRGB},
                                                                      },
                                                                  .required_min_coded_width = 2,
                                                                  .required_max_coded_width = 2,
                                                                  .required_min_coded_height = 2,
                                                                  .required_max_coded_height = 2,
                                                              },
                                                      },
                                              },
                                              fuchsia::sysmem::BufferCollectionConstraints{
                                                  .usage =
                                                      {
                                                          .cpu =
                                                              fuchsia::sysmem::cpuUsageRead |
                                                              fuchsia::sysmem::cpuUsageReadOften,
                                                      },
                                                  .has_buffer_memory_constraints = true,
                                                  .buffer_memory_constraints =
                                                      {
                                                          .ram_domain_supported = true,
                                                      },
                                              }},
                             .collection_info_ =
                                 {
                                     .buffer_count = 2,
                                     .settings =
                                         {
                                             .buffer_settings =
                                                 {
                                                     .size_bytes = 128,
                                                     .coherency_domain =
                                                         fuchsia::sysmem::CoherencyDomain::RAM,
                                                     .heap = fuchsia::sysmem::HeapType::SYSTEM_RAM,
                                                 },
                                             .has_image_format_constraints = true,
                                             .image_format_constraints =
                                                 {
                                                     .pixel_format =
                                                         {.type =
                                                              fuchsia::sysmem::PixelFormatType::
                                                                  R8G8B8A8},
                                                     .color_spaces_count = 1,
                                                     .color_space =
                                                         {fuchsia::sysmem::ColorSpace{.type =
                                                                                          fuchsia::sysmem::ColorSpaceType::SRGB}},
                                                     .min_coded_width = 0,
                                                     .max_coded_width = 16384,
                                                     .min_coded_height = 0,
                                                     .max_coded_height = 16384,
                                                     .min_bytes_per_row = 4,
                                                     .max_bytes_per_row = 4294967295,
                                                     .bytes_per_row_divisor = 64,
                                                     .start_offset_divisor = 4,
                                                     .required_min_coded_width = 2,
                                                     .required_max_coded_width = 2,
                                                     .required_min_coded_height = 2,
                                                     .required_max_coded_height = 2,
                                                     .required_min_bytes_per_row = 4294967295,
                                                     .required_max_bytes_per_row = 0,
                                                 }},
                                     .buffers =
                                         {
                                             fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(4096)},
                                             fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(4096)},
                                         },
                                 }}));

  return result;
}

std::list<std::unique_ptr<FakeSysmem::Expectations>>
MediaPlayerTests::BearVideoImageSysmemExpectations() {
  std::list<std::unique_ptr<FakeSysmem::Expectations>> result;

  // Video buffers
  result.push_back(std::make_unique<FakeSysmem::Expectations>(FakeSysmem::Expectations{
      .constraints_ =
          {fuchsia::sysmem::BufferCollectionConstraints{
               .usage =
                   {
                       .cpu = fuchsia::sysmem::cpuUsageRead | fuchsia::sysmem::cpuUsageReadOften,
                   },
               .has_buffer_memory_constraints = true,
               .buffer_memory_constraints =
                   {
                       .ram_domain_supported = true,
                   },
           },
           fuchsia::sysmem::BufferCollectionConstraints{
               .usage =
                   {
                       .cpu = fuchsia::sysmem::cpuUsageWrite |
                              fuchsia::sysmem::cpuUsageWriteOften,
                   },
               .min_buffer_count_for_camping = 6,
               .has_buffer_memory_constraints = true,
               .buffer_memory_constraints =
                   {
                       .min_size_bytes = 1416960,
                       .ram_domain_supported = true,
                   },
               .image_format_constraints_count = 1,
               .image_format_constraints =
                   {
                       fuchsia::sysmem::ImageFormatConstraints{
                           .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::I420},
                           .color_spaces_count = 1,
                           .color_space =
                               {
                                   fuchsia::sysmem::ColorSpace{
                                       .type = fuchsia::sysmem::ColorSpaceType::REC709},
                               },
                           .required_min_coded_width = 1280,
                           .required_max_coded_width = 1280,
                           .required_min_coded_height = 738,
                           .required_max_coded_height = 738,
                       },
                   },
           }},
      .collection_info_ =
          {
              .buffer_count = 8,
              .settings = {.buffer_settings =
                               {
                                   .size_bytes = 1416960,
                                   .coherency_domain = fuchsia::sysmem::CoherencyDomain::RAM,
                                   .heap = fuchsia::sysmem::HeapType::SYSTEM_RAM,
                               },
                           .has_image_format_constraints = true,
                           .image_format_constraints =
                               {
                                   .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::I420},
                                   .color_spaces_count = 1,
                                   .color_space =
                                       {
                                           fuchsia::sysmem::ColorSpace{
                                               .type = fuchsia::sysmem::ColorSpaceType::REC709}},
                                   .min_coded_width = 0,
                                   .max_coded_width = 16384,
                                   .min_coded_height = 0,
                                   .max_coded_height = 16384,
                                   .min_bytes_per_row = 4,
                                   .max_bytes_per_row = 4294967295,
                                   .coded_width_divisor = 2,
                                   .coded_height_divisor = 2,
                                   .bytes_per_row_divisor = 16,
                                   .start_offset_divisor = 2,
                                   .required_min_coded_width = 1280,
                                   .required_max_coded_width = 1280,
                                   .required_min_coded_height = 738,
                                   .required_max_coded_height = 738,
                                   .required_min_bytes_per_row = 4294967295,
                                   .required_max_bytes_per_row = 0,
                               }},
              .buffers =
                  {
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                      fuchsia::sysmem::VmoBuffer{.vmo = CreateVmo(1417216)},
                  },
          }}));

  return result;
}

std::list<std::unique_ptr<FakeSysmem::Expectations>> MediaPlayerTests::BearSysmemExpectations() {
  auto result = BlackImageSysmemExpectations();
  result.splice(result.end(), BearVideoImageSysmemExpectations());
  return result;
}

// Play a synthetic WAV file from beginning to end.
TEST_F(MediaPlayerTests, PlayWav) {
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
// TODO(fxbug.dev/35616): Flaking.
TEST_F(MediaPlayerTests, PlayWavDelayEos) {
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
TEST_F(MediaPlayerTests, PlayWavRetainPackets) {
  fake_audio_.renderer().SetRetainPackets(true);
  fake_audio_.renderer().ExpectPackets({
      {0, 4096, 0x20c39d1e31991800},     {1024, 4096, 0xeaf137125d313800},
      {2048, 4096, 0x6162095671991800},  {3072, 4096, 0x36e551c7dd41f800},
      {4096, 4096, 0x23dcbf6fb1991800},  {5120, 4096, 0xee0a5963dd313800},
      {6144, 4096, 0x647b2ba7f1991800},  {7168, 4096, 0x39fe74195d41f800},
      {8192, 4096, 0xb3de76b931991800},  {9216, 4096, 0x7e0c10ad5d313800},
      {10240, 4096, 0xf47ce2f171991800}, {11264, 4096, 0xca002b62dd41f800},
      {12288, 4096, 0xb6f7990ab1991800}, {13312, 4096, 0x812532fedd313800},
      {14336, 4096, 0xf7960542f1991800}, {15360, 4096, 0x5cdf188f881c7800},
      {16384, 4096, 0x20c39d1e31991800}, {17408, 4096, 0xeaf137125d313800},
      {18432, 4096, 0x6162095671991800}, {19456, 4096, 0x36e551c7dd41f800},
      {20480, 4096, 0x23dcbf6fb1991800}, {21504, 4096, 0xee0a5963dd313800},
      {22528, 4096, 0x647b2ba7f1991800}, {23552, 4096, 0x39fe74195d41f800},
      {24576, 4096, 0xb3de76b931991800}, {25600, 4096, 0x7e0c10ad5d313800},
      {26624, 4096, 0xf47ce2f171991800}, {27648, 4096, 0xca002b62dd41f800},
      {28672, 4096, 0xb6f7990ab1991800}, {29696, 4096, 0x812532fedd313800},
      {30720, 4096, 0xf7960542f1991800}, {31744, 4096, 0x5cdf188f881c7800},
      {32768, 4096, 0x20c39d1e31991800}, {33792, 4096, 0xeaf137125d313800},
      {34816, 4096, 0x6162095671991800}, {35840, 4096, 0x36e551c7dd41f800},
      {36864, 4096, 0x23dcbf6fb1991800}, {37888, 4096, 0xee0a5963dd313800},
      {38912, 4096, 0x647b2ba7f1991800}, {39936, 4096, 0x39fe74195d41f800},
      {40960, 4096, 0xb3de76b931991800}, {41984, 4096, 0x7e0c10ad5d313800},
      {43008, 4096, 0xf47ce2f171991800}, {44032, 4096, 0xca002b62dd41f800},
      {45056, 4096, 0xb6f7990ab1991800}, {46080, 4096, 0x812532fedd313800},
      {47104, 4096, 0xf7960542f1991800}, {48128, 4096, 0x5cdf188f881c7800},
      {49152, 4096, 0x20c39d1e31991800}, {50176, 4096, 0xeaf137125d313800},
      {51200, 4096, 0x6162095671991800}, {52224, 4096, 0x36e551c7dd41f800},
      {53248, 4096, 0x23dcbf6fb1991800}, {54272, 4096, 0xee0a5963dd313800},
      {55296, 4096, 0x647b2ba7f1991800}, {56320, 4096, 0x39fe74195d41f800},
      {57344, 4096, 0xb3de76b931991800}, {58368, 4096, 0x7e0c10ad5d313800},
      {59392, 4096, 0xf47ce2f171991800}, {60416, 4096, 0xca002b62dd41f800},
      {61440, 4096, 0xb6f7990ab1991800}, {62464, 4096, 0x812532fedd313800},
      {63488, 2004, 0xfbff1847deca6dea},
  });

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

  // Wait a bit.
  commands_.Sleep(zx::sec(2));

  commands_.Invoke([this]() {
    // We should not be at end-of-stream in spite of waiting long enough, because the pipeline
    // has been stalled by the renderer retaining packets.
    EXPECT_FALSE(commands_.at_end_of_stream());

    // Retire packets.
    fake_audio_.renderer().SetRetainPackets(false);
  });

  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
}

// Play a short synthetic WAV file from beginning to end, retaining packets. This
// tests the ability of the player to handle the case in which the audio
// renderer not retiring packets, but all of the audio content fits into the
// payload VMO.
TEST_F(MediaPlayerTests, PlayShortWavRetainPackets) {
  fake_audio_.renderer().SetRetainPackets(true);
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

  // Wait a bit.
  commands_.Sleep(zx::sec(2));

  commands_.Invoke([this]() {
    // We should not be at end-of-stream in spite of waiting long enough, because the pipeline
    // has been stalled by the renderer retaining packets.
    EXPECT_FALSE(commands_.at_end_of_stream());

    // Retire packets.
    fake_audio_.renderer().SetRetainPackets(false);
  });

  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
}

// Play an LPCM elementary stream using |ElementarySource|
TEST_F(MediaPlayerTests, ElementarySource) {
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
TEST_F(MediaPlayerTests, ElementarySourceWithSBC) {
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
TEST_F(MediaPlayerTests, ElementarySourceWithAAC) {
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
TEST_F(MediaPlayerTests, ElementarySourceWithAACLATM) {
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
TEST_F(MediaPlayerTests, ElementarySourceWithBogus) {
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
TEST_F(MediaPlayerTests, PlayBear) {
  // It would be great to verify the audio packets here, but test bots are slow enough that
  // audio packets sometimes get dropped due to lack of VMO space for the audio renderer.

  fake_sysmem_.SetExpectations(BearSysmemExpectations());

  fake_audio_.renderer().ExpectPackets(
      {{1024, 8192, 0x0a68b3995a50a648},   {2048, 8192, 0x93bf522ee77e9d50},
       {3072, 8192, 0x89cc3bcedd6034be},   {4096, 8192, 0x40931af9f379dd00},
       {5120, 8192, 0x79dc4cfe61738988},   {6144, 8192, 0x2c831d823db62908},
       {7168, 8192, 0x71561155059a2950},   {8192, 8192, 0x4581449f2e040ff0},
       {9216, 8192, 0xb0429eeed8b7424e},   {10240, 8192, 0x5e7007ebe169fcc0},
       {11264, 8192, 0x585fe50f30788fd8},  {12288, 8192, 0x7cba92a4ecaf59a2},
       {13312, 8192, 0x8521ccbccc4d771e},  {14336, 8192, 0x5694e56b0fd93cc8},
       {15360, 8192, 0x14abced62917c788},  {16384, 8192, 0x8e7f3918fa412a02},
       {17408, 8192, 0xf095ec04d2238644},  {18432, 8192, 0x886cab3f4e3f9610},
       {19456, 8192, 0x874a3d8d0f4e2190},  {20480, 8192, 0x1f70d5763dadf9ac},
       {21504, 8192, 0x2619ff3221cbab46},  {22528, 8192, 0x33aa3594808f6b10},
       {23552, 8192, 0x2da9b93cacd110a4},  {24576, 8192, 0x2f0def95d105b68c},
       {25600, 8192, 0xef9acc73b96291c4},  {26624, 8192, 0xca8ed12c8f4b7b06},
       {27648, 8192, 0x0ea5eddd4cc5e3bc},  {28672, 8192, 0xafe4007e4779438e},
       {29696, 8192, 0xcefebc7fe3257f9e},  {30720, 8192, 0x4294978d0dc213ee},
       {31744, 8192, 0x53ca41b8a5175774},  {32768, 8192, 0x9a16b082e9e5a95e},
       {33792, 8192, 0x1a849b5e1f4ee80a},  {34816, 8192, 0xd1741d4e44972fea},
       {35840, 8192, 0x7ecf5a82a4adf9a6},  {36864, 8192, 0x2878988793205f22},
       {37888, 8192, 0x35a41b25f24ec2b8},  {38912, 8192, 0x2714de582b48ebc6},
       {39936, 8192, 0xc8fdea128f0285f4},  {40960, 8192, 0xc5ab19b2405542ca},
       {41984, 8192, 0x5d5d781722ba0392},  {43008, 8192, 0x02fe263969ba81a6},
       {44032, 8192, 0x1acc5b7c24d197d4},  {45056, 8192, 0x18d713e058acfec8},
       {46080, 8192, 0x83573b4a6f02c8da},  {47104, 8192, 0xacffcaaff833e850},
       {48128, 8192, 0xa0cffe3e485c46c4},  {49152, 8192, 0xffd5680f78b7f7a2},
       {50176, 8192, 0xc950e93a5272cda8},  {51200, 8192, 0x375e4dc1dc28eea4},
       {52224, 8192, 0x5648dd0ed9d9d9d4},  {53248, 8192, 0xac945623bf04f5b6},
       {54272, 8192, 0x3cff2936986fcdc8},  {55296, 8192, 0xbc049d18bdcca182},
       {56320, 8192, 0x8d3646f2e29da29c},  {57344, 8192, 0xb5e72da09cd9f5b4},
       {58368, 8192, 0x8597406852caa548},  {59392, 8192, 0x5221d69a113d9688},
       {60416, 8192, 0xc4c0bdef8e07fb12},  {61440, 8192, 0x804e43c36110196e},
       {62464, 8192, 0xd1d3ae38126dd618},  {63488, 8192, 0x846d01cfa3be6500},
       {64512, 8192, 0xecca760a67eff43a},  {65536, 8192, 0x6624720182df5730},
       {66560, 8192, 0x41eb3d61d94b2224},  {67584, 8192, 0x015efd07043b4e4c},
       {68608, 8192, 0x2d4d9823e0e63b64},  {69632, 8192, 0xd5a845cbf966e23a},
       {70656, 8192, 0x24c6ccf454693f72},  {71680, 8192, 0x368bea38398d5ecc},
       {72704, 8192, 0x3602a6b0602a9458},  {73728, 8192, 0x48ea44911825e784},
       {74752, 8192, 0x53e549d74eb26de0},  {75776, 8192, 0x3f7f7f5c7ee3d14e},
       {76800, 8192, 0xdcafb6baa55625f6},  {77824, 8192, 0x472b007f3bc3c45c},
       {78848, 8192, 0x53a8ecc580fff982},  {79872, 8192, 0xf59a57769900ca62},
       {80896, 8192, 0xcc380147f73a1528},  {81920, 8192, 0x4f4b79f5ad21e67e},
       {82944, 8192, 0xcee2192004c8066c},  {83968, 8192, 0x84672c98f8a1da4c},
       {84992, 8192, 0x229246edd7b6c31c},  {86016, 8192, 0x3f3f4d7f8fcd62b4},
       {87040, 8192, 0x46bc2a4e9e6d40ca},  {88064, 8192, 0xa6901df8e4afcc48},
       {89088, 8192, 0x8e96017b64980fd8},  {90112, 8192, 0xdd9001f337c6a932},
       {91136, 8192, 0xac5913cdd15b8a72},  {92160, 8192, 0xd9d59a367d561d4c},
       {93184, 8192, 0xa76421aaa4b469c8},  {94208, 8192, 0x2e27a33a898c0056},
       {95232, 8192, 0xb71592d727280bc0},  {96256, 8192, 0xb73b2e5a682cbf60},
       {97280, 8192, 0x36d9f03861277c10},  {98304, 8192, 0xffa1d33f4aea2e40},
       {99328, 8192, 0x4359627a59f6552e},  {100352, 8192, 0x82a76e3c810aee68},
       {101376, 8192, 0x60066a5773c5dee2}, {102400, 8192, 0x809989d272e85654},
       {103424, 8192, 0xd1cdd52e37d58702}, {104448, 8192, 0xe332d1115653f36c},
       {105472, 8192, 0xa1189ac1a76c3bd0}, {106496, 8192, 0xaa20304ceb8e6daa},
       {107520, 8192, 0x913ac8dcdc5cef52}, {108544, 8192, 0x891883b9326cd0f4},
       {109568, 8192, 0xe8fbce45cf3990a4}, {110592, 8192, 0xc9301a9ef899455c},
       {111616, 8192, 0x56cd5306b56e027a}, {112640, 8192, 0x5a1b088bce12b0f8},
       {113664, 8192, 0xc697191375e99274}, {114688, 8192, 0x4d0f0798a59771c4},
       {115712, 8192, 0x6571a4ff90e63490}, {116736, 8192, 0x20ffb62fff517f00},
       {117760, 8192, 0x20ffb62fff517f00}, {118784, 8192, 0x20ffb62fff517f00},
       {119808, 8192, 0x20ffb62fff517f00}, {120832, 8192, 0x20ffb62fff517f00}});
  fake_audio_.renderer().ExpectPackets({
      {1024, 8192, 0x0a278d9cb22e24c4},   {2048, 8192, 0xcac15dcabac1d262},
      {3072, 8192, 0x8e9eab619d7bc6a4},   {4096, 8192, 0x71adf7d7c8ddda7c},
      {5120, 8192, 0x0b3e51a900e6b0b6},   {6144, 8192, 0xed15ccab8f919470},
      {7168, 8192, 0x5e05e1a9b698264a},   {8192, 8192, 0xc919913a6963fea0},
      {9216, 8192, 0xd730eb49fd9c2376},   {10240, 8192, 0xaf4ceb1d94d584c6},
      {11264, 8192, 0xfecfb2a9df17a76c},  {12288, 8192, 0xde6a59ccf4dcca3c},
      {13312, 8192, 0x6a14865dd4bdaaa4},  {14336, 8192, 0xb9c06bd23aad59ba},
      {15360, 8192, 0x73f7c3addfca55e8},  {16384, 8192, 0x54930755a8f84adc},
      {17408, 8192, 0xe17b36f6c320621c},  {18432, 8192, 0x13f8cd5770c886f8},
      {19456, 8192, 0xdef270a1a0dad9c4},  {20480, 8192, 0x6efa5964111fadca},
      {21504, 8192, 0xb9313a6eac0b6dd0},  {22528, 8192, 0x4914caf746ccaa9a},
      {23552, 8192, 0x393c50c258672e14},  {24576, 8192, 0x75e9332543953a78},
      {25600, 8192, 0x44a8a5c7a4abab58},  {26624, 8192, 0x390446bb3cde21e8},
      {27648, 8192, 0xafb517f9859b905c},  {28672, 8192, 0x7e77976f6716522e},
      {29696, 8192, 0x670c0c0cb987aea4},  {30720, 8192, 0xd06bec5cd8bd18ce},
      {31744, 8192, 0x141c254b6d0963e4},  {32768, 8192, 0xdefd2fa7df82b1e2},
      {33792, 8192, 0xde8f61742caf027c},  {34816, 8192, 0x08ce0812cf058bbe},
      {35840, 8192, 0xfb49f3e4048ca322},  {36864, 8192, 0x3e3c5184625d7254},
      {37888, 8192, 0xb42615e5195ec9d0},  {38912, 8192, 0x98fe6b846b18fd3e},
      {39936, 8192, 0xabc839a0dc15aeb8},  {40960, 8192, 0x7c554a1a5561e17e},
      {41984, 8192, 0xa62e727ba5d3b4f0},  {43008, 8192, 0xec8349112c6a258a},
      {44032, 8192, 0x35e8fec98685b3a2},  {45056, 8192, 0x3b4134f45106db6e},
      {46080, 8192, 0x5bf0f269bfd9f626},  {47104, 8192, 0x23396da9ad243d80},
      {48128, 8192, 0x07d77b07fa46d0fc},  {49152, 8192, 0xf3853767e72fb61e},
      {50176, 8192, 0xf41d82a8796382f8},  {51200, 8192, 0x89fce751cfca4b1e},
      {52224, 8192, 0xd6cd259ff40aa754},  {53248, 8192, 0xb080ba61bdef1c68},
      {54272, 8192, 0xa39fc3f6caa70744},  {55296, 8192, 0x62fb3c9f1d42ed64},
      {56320, 8192, 0xb451c5430cfbd69e},  {57344, 8192, 0x3466118c502b5c78},
      {58368, 8192, 0xad8e1d230191eb42},  {59392, 8192, 0x21e0b5b1d2195832},
      {60416, 8192, 0xbec97338cde9a1fe},  {61440, 8192, 0xd5e446d761cc0ab0},
      {62464, 8192, 0x056f6691bcbfd4a4},  {63488, 8192, 0x053c435831600e4e},
      {64512, 8192, 0x4b1cf5807abbbee8},  {65536, 8192, 0xf26ffef5b10b6484},
      {66560, 8192, 0x19c1f5d9443cf372},  {67584, 8192, 0x0fd3fd10ff9b8aa2},
      {68608, 8192, 0xe0be326193ecb916},  {69632, 8192, 0x825dc183bbedb7e8},
      {70656, 8192, 0xfa114b4764168094},  {71680, 8192, 0xd8fc2dea3718215c},
      {72704, 8192, 0x08ca42a72b05b80c},  {73728, 8192, 0xaaf1e1d5ddedb810},
      {74752, 8192, 0x5413ead39d5e71ec},  {75776, 8192, 0xfd5c65e39bfbfb68},
      {76800, 8192, 0x67c636364a50ea30},  {77824, 8192, 0x44f4631e15fb7ff4},
      {78848, 8192, 0x37e151f6a9118144},  {79872, 8192, 0xe70dee80e1ed5022},
      {80896, 8192, 0xf8f6481770932022},  {81920, 8192, 0xb7e965e4b58ebcfa},
      {82944, 8192, 0x306a397fcf2530cc},  {83968, 8192, 0xebb4b9bbdc6b9318},
      {84992, 8192, 0x9113ad1fd26cb224},  {86016, 8192, 0x9e72615a126b49ee},
      {87040, 8192, 0xb9f1664e9e3dc5a4},  {88064, 8192, 0xc556aac1f3fe3caa},
      {89088, 8192, 0xee381da85b7b83cc},  {90112, 8192, 0x950fcf1cda541034},
      {91136, 8192, 0x6a5a07c427f1fe6c},  {92160, 8192, 0x5af136ce64321880},
      {93184, 8192, 0x3f3fb75a7897c844},  {94208, 8192, 0x3ab371db7ee7f98a},
      {95232, 8192, 0x25f0512b17f46140},  {96256, 8192, 0x2285dc7571eacce0},
      {97280, 8192, 0xeefc39ea62c1fb1e},  {98304, 8192, 0x20f512e77dd9fe0a},
      {99328, 8192, 0x326b5f83c8a86f4a},  {100352, 8192, 0x61300ef79004b710},
      {101376, 8192, 0x6b041230cc0b6d42}, {102400, 8192, 0xc7b5eeba766e0766},
      {103424, 8192, 0xdbfb1ce6817378e6}, {104448, 8192, 0x56d952dede20aa4a},
      {105472, 8192, 0x6f0af97f0e2c0596}, {106496, 8192, 0x4b3899d80facb334},
      {107520, 8192, 0xcad9187ad9012090}, {108544, 8192, 0xfe0a0cbf07574a02},
      {109568, 8192, 0x6b9df9d466bdd4dc}, {110592, 8192, 0xc5af898593ea8ed4},
      {111616, 8192, 0x3614f04ce50681aa}, {112640, 8192, 0xcd532f60385aa956},
      {113664, 8192, 0xff38b9ed5a6b5dea}, {114688, 8192, 0x105bd90b243c3d84},
      {115712, 8192, 0x983976476c930d30}, {116736, 8192, 0xa597cd2125da8100},
      {117760, 8192, 0xa597cd2125da8100}, {118784, 8192, 0xa597cd2125da8100},
      {119808, 8192, 0xa597cd2125da8100}, {120832, 8192, 0xa597cd2125da8100}});

  fake_scenic_.session().SetExpectations(
      1,
      {
          .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8},
          .coded_width = 2,
          .coded_height = 2,
          .bytes_per_row = 8,
          .display_width = 2,
          .display_height = 2,
          .color_space = {.type = fuchsia::sysmem::ColorSpaceType::SRGB},
          .has_pixel_aspect_ratio = true,
      },
      {
          .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::I420},
          .coded_width = 1280,
          .coded_height = 738,
          .bytes_per_row = 1280,
          .display_width = 1280,
          .display_height = 720,
          .color_space = {.type = fuchsia::sysmem::ColorSpaceType::REC709},
          .has_pixel_aspect_ratio = true,
      },
      {
          {0, 944640, 0xe22305b43e20ba47},          {146479375, 944640, 0x66ae7cd1ab593c8e},
          {179846042, 944640, 0x8893faaea28f39bc},  {213212708, 944640, 0x88508b0a34efffad},
          {246579375, 944640, 0x3a63c81772b70383},  {279946042, 944640, 0x3780c4550621ebe0},
          {313312708, 944640, 0x4f921c4320a6417f},  {346679375, 944640, 0x4e9a21647e4929be},
          {380046042, 944640, 0xe7e665c795955c15},  {413412708, 944640, 0x3c3aedc1d6683aa4},
          {446779375, 944640, 0xfe9e286a635fb73d},  {480146042, 944640, 0x47e6f4f1abff1b7e},
          {513512708, 944640, 0x84f562dcd46197a5},  {546879375, 944640, 0xf38b34e69d27cbc9},
          {580246042, 944640, 0xee2998da3599b399},  {613612708, 944640, 0x524da51958ef48d3},
          {646979375, 944640, 0x062586602fe0a479},  {680346042, 944640, 0xc32d430e92ae479c},
          {713712708, 944640, 0x3dff5398e416dc2b},  {747079375, 944640, 0xd3c76266c63bd4c3},
          {780446042, 944640, 0xc3241587b5491999},  {813812708, 944640, 0xfd3abe1fbe877da2},
          {847179375, 944640, 0x1a3bd139a0f8460b},  {880546042, 944640, 0x11f585d7e68bda67},
          {913912708, 944640, 0xecd344c5043e29ae},  {947279375, 944640, 0x7ae6b259c3b7f093},
          {980646042, 944640, 0x5d49bfa6c196c9d1},  {1014012708, 944640, 0xe83a44b02cac86f6},
          {1047379375, 944640, 0xffad44c6d3f60005}, {1080746042, 944640, 0x85d1372b40b214c4},
          {1114112708, 944640, 0x9b6f88950ead9041}, {1147479375, 944640, 0x1396882cb6f522a1},
          {1180846042, 944640, 0x07815d4ef90b1507}, {1214212708, 944640, 0x424879e928edc717},
          {1247579375, 944640, 0xd623f27e3773245f}, {1280946042, 944640, 0x47581df2a2e350ff},
          {1314312708, 944640, 0xb836a1cbbae59a31}, {1347679375, 944640, 0xe6d7ce3f416411ea},
          {1381046042, 944640, 0x1c5dba765b2b85f3}, {1414412708, 944640, 0x85987a43defb3ead},
          {1447779375, 944640, 0xe66b3d70ca2358db}, {1481146042, 944640, 0x2b7e765a1f2245de},
          {1514512708, 944640, 0x9e79fedce712de01}, {1547879375, 944640, 0x7ad7078f8731e4f0},
          {1581246042, 944640, 0x91ac3c20c4d4e497}, {1614612708, 944640, 0xdb7c8209e5b3a2f4},
          {1647979375, 944640, 0xd47a9314da3ddec9}, {1681346042, 944640, 0x00c1c1f8e8570386},
          {1714712708, 944640, 0x1b603a5644b00e7f}, {1748079375, 944640, 0x15c18419b83f5a54},
          {1781446042, 944640, 0x0038ff1808d201c7}, {1814812708, 944640, 0xe7b2592675d2002a},
          {1848179375, 944640, 0x55ef9a4ba7570494}, {1881546042, 944640, 0x14b6c92ae0fde6a9},
          {1914912708, 944640, 0x3f05f2378c5d06c2}, {1948279375, 944640, 0x04f246ec6c3f0ab9},
          {1981646042, 944640, 0x829529ce2d0a95cd}, {2015012708, 944640, 0xc0eee6a564624169},
          {2048379375, 944640, 0xdd31903bdc9c909f}, {2081746042, 944640, 0x989727e8fcd13cca},
          {2115112708, 944640, 0x9e6b6fe9d1b02649}, {2148479375, 944640, 0x01cfc5a96079d823},
          {2181846042, 944640, 0x90ee949821bfed16}, {2215212708, 944640, 0xf6e66a48b2c977cc},
          {2248579375, 944640, 0xb5a1d79f1401e1a6}, {2281946042, 944640, 0x89e8ca8aa0b24bef},
          {2315312708, 944640, 0xd7e384493250e13b}, {2348679375, 944640, 0x7c042bbc365297eb},
          {2382046042, 944640, 0xfaf92184251ecbf4}, {2415412708, 944640, 0x0edcf5f479f9ec39},
          {2448779375, 944640, 0x59c165487d90fbb3}, {2482146042, 944640, 0xd4fbf15095e6b728},
          {2515512708, 944640, 0x6a05e676671df8e1}, {2548879375, 944640, 0x44d653ed72393e1c},
          {2582246042, 944640, 0x912f720f4c904527}, {2615612708, 944640, 0xe4ca7bc6919d1e70},
          {2648979375, 944640, 0x6cde61420e173a62}, {2682346042, 944640, 0xfe0d7d86d0b57044},
          {2715712708, 944640, 0x2d96bc09b6303a4b}, {2749079375, 944640, 0x2cdaab788c93a466},
          {2782446042, 944640, 0x979b90a096e76dbb}, {2815812708, 944640, 0x851ccb01ea035f4e},
      });

  CreateView();
  commands_.SetFile(kBearFilePath);
  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
  EXPECT_TRUE(fake_sysmem_.expected());
}  // namespace test

// Play an opus file from beginning to end.
TEST_F(MediaPlayerTests, PlayOpus) {
  // The decoder works a bit differently on x64 vs arm64, hence the two lists here.
  fake_audio_.renderer().ExpectPackets({{-336, 1296, 0x47ff30edd64831d6},
                                        {312, 1920, 0xcc4016bbb348e52b},
                                        {1272, 1920, 0xe54a89514c636028},
                                        {2232, 1920, 0x8ef31ce86009d7da},
                                        {3192, 1920, 0x36490fe70ca3bb81},
                                        {4152, 1920, 0x4a8bdd8e9c2f42bb},
                                        {5112, 1920, 0xbc8cea1839f0299e},
                                        {6072, 1920, 0x868a68451d7ab814},
                                        {7032, 1920, 0x84ac9b11a685a9a9},
                                        {7992, 1920, 0xe4359c110afe8adb},
                                        {8952, 1920, 0x2092c7fbf2ff0f0c},
                                        {9912, 1920, 0x8002d77665736d63},
                                        {10872, 1920, 0x541b415fbdc7b268},
                                        {11832, 1920, 0xe81ef757a5953573},
                                        {12792, 1920, 0xbc70aba0ed44f7dc}});
  fake_audio_.renderer().ExpectPackets({{-336, 1296, 0xbf1f56243e245a2c},
                                        {312, 1920, 0x670e69ee3076c4b2},
                                        {1272, 1920, 0xe0667e312e65207d},
                                        {2232, 1920, 0x291ffa6baf5dd2b1},
                                        {3192, 1920, 0x1b408d840e27bcc1},
                                        {4152, 1920, 0xdbf5034a75bc761b},
                                        {5112, 1920, 0x46fa968eb705415b},
                                        {6072, 1920, 0x9f47ee9cbb3c814c},
                                        {7032, 1920, 0x7256d4c58d7afe56},
                                        {7992, 1920, 0xb2a7bc50ce80c898},
                                        {8952, 1920, 0xb314415fd9c3a694},
                                        {9912, 1920, 0x34d9ce067ffacc37},
                                        {10872, 1920, 0x661cc8ec834fb30a},
                                        {11832, 1920, 0x05fd64442f53c5cc},
                                        {12792, 1920, 0x3e2a98426c8680d0}});

  commands_.SetFile(kOpusFilePath);
  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
}

// Play a real A/V file from beginning to end, retaining audio packets. This
// tests the ability of the player to handle the case in which the audio
// renderer is holding on to packets for too long.
TEST_F(MediaPlayerTests, PlayBearRetainAudioPackets) {
  fake_sysmem_.SetExpectations(BearSysmemExpectations());

  CreateView();
  fake_audio_.renderer().SetRetainPackets(true);

  commands_.SetFile(kBearFilePath);
  commands_.Play();

  // Wait a bit.
  commands_.Sleep(zx::sec(2));

  commands_.Invoke([this]() {
    // We should not be at end-of-stream in spite of waiting long enough, because the pipeline
    // has been stalled by the renderer retaining packets.
    EXPECT_FALSE(commands_.at_end_of_stream());

    // Retire packets.
    fake_audio_.renderer().SetRetainPackets(false);
  });

  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
  EXPECT_TRUE(fake_sysmem_.expected());
}

// Regression test for US-544.
TEST_F(MediaPlayerTests, RegressionTestUS544) {
  fake_sysmem_.SetExpectations(BearSysmemExpectations());

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
  EXPECT_TRUE(fake_sysmem_.expected());
}

// Regression test for QA-539.
// Verifies that the player can play two files in a row.
TEST_F(MediaPlayerTests, RegressionTestQA539) {
  auto sysmem_expectations = BearSysmemExpectations();
  // Expect the video image buffers to be allocated again.
  sysmem_expectations.splice(sysmem_expectations.end(), BearVideoImageSysmemExpectations());
  fake_sysmem_.SetExpectations(std::move(sysmem_expectations));

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
  EXPECT_TRUE(fake_sysmem_.expected());
}

// Play an LPCM elementary stream using |ElementarySource|. We delay calling SetSource to ensure
// that the SimpleStreamSink defers taking any action until it's properly connected.
TEST_F(MediaPlayerTests, ElementarySourceDeferred) {
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

  sink_feeder_.Init(std::move(sink), kSinkFeedSize, kSamplesPerFrame * sizeof(int16_t),
                    kSinkFeedMaxPacketSize, kSinkFeedMaxPacketCount);

  // Here we're upcasting from a
  // |fidl::InterfaceHandle<fuchsia::media::playback::ElementarySource>| to a
  // |fidl::InterfaceHandle<fuchsia::media::playback::Source>| the only way we
  // currently can. The compiler has no way of knowing whether this is
  // legit.
  // TODO(dalesat): Do this safely once FIDL-329 is fixed.
  player_->SetSource(fidl::InterfaceHandle<fuchsia::media::playback::Source>(
      elementary_source.Unbind().TakeChannel()));

  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_FALSE(sink_connection_closed_);
}

// Play a real A/V file from beginning to end and rate 2.0.
TEST_F(MediaPlayerTests, PlayBear2) {
  // It would be great to verify the audio packets here, but test bots are slow enough that
  // audio packets sometimes get dropped due to lack of VMO space for the audio renderer.

  fake_sysmem_.SetExpectations(BearSysmemExpectations());

  fake_audio_.renderer().ExpectPackets({{1024, 8192, 0x0a68b3995a50a648},
                                        {2048, 8192, 0x93bf522ee77e9d50},
                                        {3072, 8192, 0x89cc3bcedd6034be},
                                        {4096, 8192, 0x40931af9f379dd00}});
  fake_audio_.renderer().ExpectPackets({{1024, 8192, 0x0a278d9cb22e24c4},
                                        {2048, 8192, 0xcac15dcabac1d262},
                                        {3072, 8192, 0x8e9eab619d7bc6a4},
                                        {4096, 8192, 0x71adf7d7c8ddda7c}});

  fake_scenic_.session().SetExpectations(
      1,
      {
          .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::R8G8B8A8},
          .coded_width = 2,
          .coded_height = 2,
          .bytes_per_row = 8,
          .display_width = 2,
          .display_height = 2,
          .color_space = {.type = fuchsia::sysmem::ColorSpaceType::SRGB},
          .has_pixel_aspect_ratio = true,
      },
      {
          .pixel_format = {.type = fuchsia::sysmem::PixelFormatType::I420},
          .coded_width = 1280,
          .coded_height = 738,
          .bytes_per_row = 1280,
          .display_width = 1280,
          .display_height = 720,
          .color_space = {.type = fuchsia::sysmem::ColorSpaceType::REC709},
          .has_pixel_aspect_ratio = true,
      },
      {
          {0, 944640, 0xe22305b43e20ba47},          {112751833, 944640, 0x66ae7cd1ab593c8e},
          {129435167, 944640, 0x8893faaea28f39bc},  {146118500, 944640, 0x88508b0a34efffad},
          {162801833, 944640, 0x3a63c81772b70383},  {179485167, 944640, 0x3780c4550621ebe0},
          {196168500, 944640, 0x4f921c4320a6417f},  {212851833, 944640, 0x4e9a21647e4929be},
          {229535167, 944640, 0xe7e665c795955c15},  {246218500, 944640, 0x3c3aedc1d6683aa4},
          {262901833, 944640, 0xfe9e286a635fb73d},  {279585167, 944640, 0x47e6f4f1abff1b7e},
          {296268500, 944640, 0x84f562dcd46197a5},  {312951833, 944640, 0xf38b34e69d27cbc9},
          {329635167, 944640, 0xee2998da3599b399},  {346318500, 944640, 0x524da51958ef48d3},
          {363001833, 944640, 0x062586602fe0a479},  {379685167, 944640, 0xc32d430e92ae479c},
          {396368500, 944640, 0x3dff5398e416dc2b},  {413051833, 944640, 0xd3c76266c63bd4c3},
          {429735167, 944640, 0xc3241587b5491999},  {446418500, 944640, 0xfd3abe1fbe877da2},
          {463101833, 944640, 0x1a3bd139a0f8460b},  {479785167, 944640, 0x11f585d7e68bda67},
          {496468500, 944640, 0xecd344c5043e29ae},  {513151833, 944640, 0x7ae6b259c3b7f093},
          {529835167, 944640, 0x5d49bfa6c196c9d1},  {546518500, 944640, 0xe83a44b02cac86f6},
          {563201833, 944640, 0xffad44c6d3f60005},  {579885167, 944640, 0x85d1372b40b214c4},
          {596568500, 944640, 0x9b6f88950ead9041},  {613251833, 944640, 0x1396882cb6f522a1},
          {629935167, 944640, 0x07815d4ef90b1507},  {646618500, 944640, 0x424879e928edc717},
          {663301833, 944640, 0xd623f27e3773245f},  {679985167, 944640, 0x47581df2a2e350ff},
          {696668500, 944640, 0xb836a1cbbae59a31},  {713351833, 944640, 0xe6d7ce3f416411ea},
          {730035167, 944640, 0x1c5dba765b2b85f3},  {746718500, 944640, 0x85987a43defb3ead},
          {763401833, 944640, 0xe66b3d70ca2358db},  {780085167, 944640, 0x2b7e765a1f2245de},
          {796768500, 944640, 0x9e79fedce712de01},  {813451833, 944640, 0x7ad7078f8731e4f0},
          {830135167, 944640, 0x91ac3c20c4d4e497},  {846818500, 944640, 0xdb7c8209e5b3a2f4},
          {863501833, 944640, 0xd47a9314da3ddec9},  {880185167, 944640, 0x00c1c1f8e8570386},
          {896868500, 944640, 0x1b603a5644b00e7f},  {913551833, 944640, 0x15c18419b83f5a54},
          {930235167, 944640, 0x0038ff1808d201c7},  {946918500, 944640, 0xe7b2592675d2002a},
          {963601833, 944640, 0x55ef9a4ba7570494},  {980285167, 944640, 0x14b6c92ae0fde6a9},
          {996968500, 944640, 0x3f05f2378c5d06c2},  {1013651833, 944640, 0x04f246ec6c3f0ab9},
          {1030335167, 944640, 0x829529ce2d0a95cd}, {1047018500, 944640, 0xc0eee6a564624169},
          {1063701833, 944640, 0xdd31903bdc9c909f}, {1080385167, 944640, 0x989727e8fcd13cca},
          {1097068500, 944640, 0x9e6b6fe9d1b02649}, {1113751833, 944640, 0x01cfc5a96079d823},
          {1130435167, 944640, 0x90ee949821bfed16}, {1147118500, 944640, 0xf6e66a48b2c977cc},
          {1163801833, 944640, 0xb5a1d79f1401e1a6}, {1180485167, 944640, 0x89e8ca8aa0b24bef},
          {1197168500, 944640, 0xd7e384493250e13b}, {1213851833, 944640, 0x7c042bbc365297eb},
          {1230535167, 944640, 0xfaf92184251ecbf4}, {1247218500, 944640, 0x0edcf5f479f9ec39},
          {1263901833, 944640, 0x59c165487d90fbb3}, {1280585167, 944640, 0xd4fbf15095e6b728},
          {1297268500, 944640, 0x6a05e676671df8e1}, {1313951833, 944640, 0x44d653ed72393e1c},
          {1330635167, 944640, 0x912f720f4c904527}, {1347318500, 944640, 0xe4ca7bc6919d1e70},
          {1364001833, 944640, 0x6cde61420e173a62}, {1380685167, 944640, 0xfe0d7d86d0b57044},
          {1397368500, 944640, 0x2d96bc09b6303a4b}, {1414051833, 944640, 0x2cdaab788c93a466},
          {1430735167, 944640, 0x979b90a096e76dbb}, {1447418500, 944640, 0x851ccb01ea035f4e},
      });

  CreateView();
  commands_.SetFile(kBearFilePath);
  commands_.SetPlaybackRate(2.0);
  commands_.Play();
  QuitOnEndOfStream();

  Execute();
  EXPECT_TRUE(fake_audio_.renderer().expected());
  EXPECT_TRUE(fake_scenic_.session().expected());
  EXPECT_TRUE(fake_sysmem_.expected());
}

}  // namespace test
}  // namespace media_player
