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
// TODO(fxb/35616): Flaking.
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
}

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

}  // namespace test
}  // namespace media_player
