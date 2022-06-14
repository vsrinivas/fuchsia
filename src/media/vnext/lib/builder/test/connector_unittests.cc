// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/audio/cpp/fidl.h>
#include <fuchsia/audiovideo/cpp/fidl.h>
#include <fuchsia/media2/cpp/fidl.h>
#include <fuchsia/video/cpp/fidl.h>

#include <optional>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/media/vnext/lib/builder/connector.h"

namespace fuchsia::math {

bool operator==(const Size& a, const Size& b) { return a.width == b.width && a.height == b.height; }

}  // namespace fuchsia::math

namespace fuchsia::mediastreams {

bool operator==(const AudioFormat& a, const AudioFormat& b) {
  return a.sample_format == b.sample_format && a.channel_count == b.channel_count &&
         a.frames_per_second == b.frames_per_second;
}

bool operator==(const VideoFormat& a, const VideoFormat& b) {
  return a.pixel_format == b.pixel_format && a.pixel_format_modifier == b.pixel_format_modifier &&
         a.color_space == b.color_space && a.coded_size == b.coded_size &&
         a.display_size == b.display_size && a.aspect_ratio == b.aspect_ratio;
}

}  // namespace fuchsia::mediastreams

namespace fmlib {
namespace {

// Gets the koid for a handle.
template <typename T>
zx_koid_t GetKoid(const T& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.koid;
}

// Gets the peer koid for a handle.
template <typename T>
zx_koid_t GetPeerKoid(const T& handle) {
  zx_info_handle_basic_t info;
  zx_status_t status = handle.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return ZX_KOID_INVALID;
  }

  return info.related_koid;
}

template <typename Format>
struct OutputStreamParameters {
  zx::eventpair buffer_collection_token_;
  std::optional<Format> format_;
  fuchsia::mediastreams::CompressionPtr compression_;
  fuchsia::media2::PacketTimestampUnitsPtr timestamp_units_;
  fuchsia::media2::StreamSinkHandle handle_;
};

template <typename Format>
struct InputStreamParameters {
  zx::eventpair buffer_collection_token_;
  std::optional<Format> format_;
  fuchsia::mediastreams::CompressionPtr compression_;
  fuchsia::media2::PacketTimestampUnitsPtr timestamp_units_;
  fidl::InterfaceRequest<fuchsia::media2::StreamSink> request_;
};

class FakeAudioConsumer : public fuchsia::audio::Consumer {
 public:
  FakeAudioConsumer(fidl::InterfaceRequest<fuchsia::audio::Consumer> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeAudioConsumer() override = default;

  bool input_stream_connected() const {
    return !!input_stream_parameters_.buffer_collection_token_;
  }

  const InputStreamParameters<fuchsia::mediastreams::AudioFormat>& input_stream_parameters() {
    return input_stream_parameters_;
  }

  // fuchsia::audio::Consumer implementation.
  void Start(fuchsia::media2::RealTimePtr when, int64_t presentation_time,
             StartCallback callback) override {}

  void Stop(fuchsia::media2::RealOrPresentationTimePtr when, StopCallback callback) override {}

  void SetRate(fuchsia::media2::RealOrPresentationTimePtr when, float desired_rate,
               SetRateCallback callback) override {}

  void AmendPresentation(fuchsia::media2::RealOrPresentationTimePtr when, int64_t delta,
                         AmendPresentationCallback callback) override {}

  void WatchPacketLeadTime(int64_t min, int64_t max,
                           WatchPacketLeadTimeCallback callback) override {}

  void ConnectInputStream(zx::eventpair buffer_collection_token,
                          fuchsia::mediastreams::AudioFormat format,
                          fuchsia::mediastreams::CompressionPtr compression,
                          fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
                          fidl::InterfaceRequest<fuchsia::media2::StreamSink> request,
                          ConnectInputStreamCallback callback) override {
    input_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    input_stream_parameters_.format_ = std::move(format);
    input_stream_parameters_.compression_ = std::move(compression);
    input_stream_parameters_.timestamp_units_ = std::move(timestamp_units);
    input_stream_parameters_.request_ = std::move(request);
    callback(fpromise::ok());
  }

  void WatchStatus(WatchStatusCallback callback) override {}

 private:
  fidl::Binding<fuchsia::audio::Consumer> binding_;
  InputStreamParameters<fuchsia::mediastreams::AudioFormat> input_stream_parameters_;
};

class FakeVideoConsumer : public fuchsia::video::Consumer {
 public:
  FakeVideoConsumer(fidl::InterfaceRequest<fuchsia::video::Consumer> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeVideoConsumer() override = default;

  bool input_stream_connected() const {
    return !!input_stream_parameters_.buffer_collection_token_;
  }

  const InputStreamParameters<fuchsia::mediastreams::VideoFormat>& input_stream_parameters() {
    return input_stream_parameters_;
  }

  // fuchsia::video::Consumer implementation.
  void Start(fuchsia::media2::RealTimePtr when, int64_t presentation_time,
             StartCallback callback) override {}

  void Stop(fuchsia::media2::RealOrPresentationTimePtr when, StopCallback callback) override {}

  void SetRate(fuchsia::media2::RealOrPresentationTimePtr when, float desired_rate,
               SetRateCallback callback) override {}

  void AmendPresentation(fuchsia::media2::RealOrPresentationTimePtr when, int64_t delta,
                         AmendPresentationCallback callback) override {}

  void WatchPacketLeadTime(int64_t min, int64_t max,
                           WatchPacketLeadTimeCallback callback) override {}

  void ConnectInputStream(zx::eventpair buffer_collection_token,
                          fuchsia::mediastreams::VideoFormat format,
                          fuchsia::mediastreams::CompressionPtr compression,
                          fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
                          fidl::InterfaceRequest<fuchsia::media2::StreamSink> request,
                          ConnectInputStreamCallback callback) override {
    input_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    input_stream_parameters_.format_ = std::move(format);
    input_stream_parameters_.compression_ = std::move(compression);
    input_stream_parameters_.timestamp_units_ = std::move(timestamp_units);
    input_stream_parameters_.request_ = std::move(request);
    callback(fpromise::ok());
  }

  void WatchStatus(WatchStatusCallback callback) override {}

 private:
  fidl::Binding<fuchsia::video::Consumer> binding_;
  InputStreamParameters<fuchsia::mediastreams::VideoFormat> input_stream_parameters_;
};

class FakeAvProducerStream : public fuchsia::audiovideo::ProducerStream {
 public:
  FakeAvProducerStream(fidl::InterfaceRequest<fuchsia::audiovideo::ProducerStream> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeAvProducerStream() override = default;

  bool output_stream_connected() const {
    return !!output_stream_parameters_.buffer_collection_token_;
  }

  const OutputStreamParameters<fuchsia::mediastreams::MediaFormat>& output_stream_parameters() {
    return output_stream_parameters_;
  }

  // fuchsia::audiovideo::ProducerStream implementation.
  void Connect(zx::eventpair buffer_collection_token, fuchsia::media2::StreamSinkHandle handle,
               ConnectCallback callback) override {
    output_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    output_stream_parameters_.handle_ = std::move(handle);
    callback(fpromise::ok());
  }

  void Disconnect() override {}

 private:
  fidl::Binding<fuchsia::audiovideo::ProducerStream> binding_;
  OutputStreamParameters<fuchsia::mediastreams::MediaFormat> output_stream_parameters_;
};

class FakeAudioProducer : public fuchsia::audio::Producer {
 public:
  FakeAudioProducer(fidl::InterfaceRequest<fuchsia::audio::Producer> request)
      : binding_(this, std::move(request)) {
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeAudioProducer() override = default;

  bool output_stream_connected() const {
    return !!output_stream_parameters_.buffer_collection_token_;
  }

  const OutputStreamParameters<fuchsia::mediastreams::AudioFormat>& output_stream_parameters() {
    return output_stream_parameters_;
  }

  // fuchsia::audio::Producer implementation.
  void Start(fuchsia::media2::RealTimePtr when, StartCallback callback) override {}

  void Stop(fuchsia::media2::RealOrPresentationTimePtr when, StopCallback callback) override {}

  void Clear(bool hold_last_frame, zx::eventpair completion_fence) override {}

  void WatchBufferLeadTime(int64_t min, int64_t max,
                           WatchBufferLeadTimeCallback callback) override {}

  void ConnectOutputStream(zx::eventpair buffer_collection_token,
                           fuchsia::mediastreams::AudioFormat format,
                           fuchsia::media2::PacketTimestampUnits timestamp_units,
                           fuchsia::media2::StreamSinkHandle stream_sink,
                           ConnectOutputStreamCallback callback) override {
    output_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    output_stream_parameters_.format_ = std::move(format);
    output_stream_parameters_.timestamp_units_ =
        std::make_unique<fuchsia::media2::PacketTimestampUnits>(std::move(timestamp_units));
    output_stream_parameters_.handle_ = std::move(stream_sink);
    callback(fpromise::ok());
  }

  void DisconnectOutputStream() override {}

 private:
  fidl::Binding<fuchsia::audio::Producer> binding_;
  OutputStreamParameters<fuchsia::mediastreams::AudioFormat> output_stream_parameters_;
};

class FakeAudioDecoder : public fuchsia::audio::Decoder {
 public:
  static FakeAudioDecoder* instance() { return instance_; }

  FakeAudioDecoder(fuchsia::mediastreams::AudioFormat format,
                   fuchsia::mediastreams::Compression compression,
                   fidl::InterfaceRequest<fuchsia::audio::Decoder> request)
      : format_(std::move(format)),
        compression_(std::move(compression)),
        binding_(this, std::move(request)) {
    EXPECT_FALSE(!!instance_);
    instance_ = this;
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeAudioDecoder() override {
    EXPECT_EQ(this, instance_);
    instance_ = nullptr;
  }

  const fuchsia::mediastreams::AudioFormat& format() const { return format_; }

  const fuchsia::mediastreams::Compression& compression() const { return compression_; }

  bool input_stream_connected() const {
    return !!input_stream_parameters_.buffer_collection_token_;
  }

  const InputStreamParameters<fuchsia::mediastreams::MediaFormat>& input_stream_parameters() {
    return input_stream_parameters_;
  }

  bool output_stream_connected() const {
    return !!output_stream_parameters_.buffer_collection_token_;
  }

  const OutputStreamParameters<fuchsia::mediastreams::MediaFormat>& output_stream_parameters() {
    return output_stream_parameters_;
  }

  // fuchsia::audio::Decoder implementation.
  void ConnectInputStream(zx::eventpair buffer_collection_token,
                          fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
                          fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
                          ConnectInputStreamCallback callback) override {
    input_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    input_stream_parameters_.timestamp_units_ = std::move(timestamp_units);
    input_stream_parameters_.request_ = std::move(stream_sink_request);
    callback(fpromise::ok());

    binding_.events().OnNewOutputStreamAvailable(fidl::Clone(format_), std::move(timestamp_units));
  }

  void ConnectOutputStream(zx::eventpair buffer_collection_token,
                           fuchsia::media2::StreamSinkHandle stream_sink,
                           ConnectOutputStreamCallback callback) override {
    output_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    output_stream_parameters_.handle_ = std::move(stream_sink);
    callback(fpromise::ok());
  }

  void DisconnectOutputStream() override { output_stream_parameters_.handle_ = nullptr; }

 private:
  static FakeAudioDecoder* instance_;
  fuchsia::mediastreams::AudioFormat format_;
  fuchsia::mediastreams::Compression compression_;
  fidl::Binding<fuchsia::audio::Decoder> binding_;
  InputStreamParameters<fuchsia::mediastreams::MediaFormat> input_stream_parameters_;
  OutputStreamParameters<fuchsia::mediastreams::MediaFormat> output_stream_parameters_;
};

// static
FakeAudioDecoder* FakeAudioDecoder::instance_ = nullptr;

class FakeAudioDecoderCreator : public fuchsia::audio::DecoderCreator {
 public:
  class Binder : public ServiceBinder {
   public:
    Binder() = default;

    ~Binder() override = default;

    void Bind(zx::channel channel) override { new FakeAudioDecoderCreator(std::move(channel)); }
  };

  FakeAudioDecoderCreator(zx::channel channel) : binding_(this, std::move(channel)) {
    binding_.set_error_handler([this](zx_status_t status) {
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
      delete this;
    });
  }

  ~FakeAudioDecoderCreator() override = default;

  // fuchsia::audio::DecoderCreator implementation.
  void Create(fuchsia::mediastreams::AudioFormat format,
              fuchsia::mediastreams::Compression compression,
              fidl::InterfaceRequest<fuchsia::audio::Decoder> request) override {
    new FakeAudioDecoder(std::move(format), std::move(compression), std::move(request));
  }

 private:
  fidl::Binding<fuchsia::audio::DecoderCreator> binding_;
};

class FakeVideoDecoder : public fuchsia::video::Decoder {
 public:
  static FakeVideoDecoder* instance() { return instance_; }

  FakeVideoDecoder(fuchsia::mediastreams::VideoFormat format,
                   fuchsia::mediastreams::Compression compression,
                   fidl::InterfaceRequest<fuchsia::video::Decoder> request)
      : format_(std::move(format)),
        compression_(std::move(compression)),
        binding_(this, std::move(request)) {
    EXPECT_FALSE(!!instance_);
    instance_ = this;
    binding_.set_error_handler([this](zx_status_t status) { delete this; });
  }

  ~FakeVideoDecoder() override {
    EXPECT_EQ(this, instance_);
    instance_ = nullptr;
  }

  const fuchsia::mediastreams::VideoFormat& format() const { return format_; }

  const fuchsia::mediastreams::Compression& compression() const { return compression_; }

  bool input_stream_connected() const {
    return !!input_stream_parameters_.buffer_collection_token_;
  }

  const InputStreamParameters<fuchsia::mediastreams::MediaFormat>& input_stream_parameters() {
    return input_stream_parameters_;
  }

  bool output_stream_connected() const {
    return !!output_stream_parameters_.buffer_collection_token_;
  }

  const OutputStreamParameters<fuchsia::mediastreams::MediaFormat>& output_stream_parameters() {
    return output_stream_parameters_;
  }

  // fuchsia::video::Decoder implementation.
  void ConnectInputStream(zx::eventpair buffer_collection_token,
                          fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
                          fidl::InterfaceRequest<fuchsia::media2::StreamSink> stream_sink_request,
                          ConnectInputStreamCallback callback) override {
    input_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    input_stream_parameters_.timestamp_units_ = std::move(timestamp_units);
    input_stream_parameters_.request_ = std::move(stream_sink_request);
    callback(fpromise::ok());

    binding_.events().OnNewOutputStreamAvailable(fidl::Clone(format_), std::move(timestamp_units));
  }

  void ConnectOutputStream(zx::eventpair buffer_collection_token,
                           fuchsia::media2::StreamSinkHandle stream_sink,
                           ConnectOutputStreamCallback callback) override {
    output_stream_parameters_.buffer_collection_token_ = std::move(buffer_collection_token);
    output_stream_parameters_.handle_ = std::move(stream_sink);
    callback(fpromise::ok());
  }

  void DisconnectOutputStream() override { output_stream_parameters_.handle_ = nullptr; }

 private:
  static FakeVideoDecoder* instance_;
  fuchsia::mediastreams::VideoFormat format_;
  fuchsia::mediastreams::Compression compression_;
  fidl::Binding<fuchsia::video::Decoder> binding_;
  InputStreamParameters<fuchsia::mediastreams::MediaFormat> input_stream_parameters_;
  OutputStreamParameters<fuchsia::mediastreams::MediaFormat> output_stream_parameters_;
};

// static
FakeVideoDecoder* FakeVideoDecoder::instance_ = nullptr;

class FakeVideoDecoderCreator : public fuchsia::video::DecoderCreator {
 public:
  class Binder : public ServiceBinder {
   public:
    Binder() = default;

    ~Binder() override = default;

    void Bind(zx::channel channel) override { new FakeVideoDecoderCreator(std::move(channel)); }
  };

  FakeVideoDecoderCreator(zx::channel channel) : binding_(this, std::move(channel)) {
    binding_.set_error_handler([this](zx_status_t status) {
      EXPECT_EQ(ZX_ERR_PEER_CLOSED, status);
      delete this;
    });
  }

  ~FakeVideoDecoderCreator() override = default;

  // fuchsia::video::DecoderCreator implementation.
  void Create(fuchsia::mediastreams::VideoFormat format,
              fuchsia::mediastreams::Compression compression,
              fidl::InterfaceRequest<fuchsia::video::Decoder> request) override {
    new FakeVideoDecoder(std::move(format), std::move(compression), std::move(request));
  }

 private:
  fidl::Binding<fuchsia::video::DecoderCreator> binding_;
};

class ConnectorTest : public fuchsia::media2::BufferProvider, public gtest::RealLoopFixture {
 protected:
  ConnectorTest() : thread_(Thread::CreateForLoop(loop())), service_provider_(thread_) {
    service_provider_.RegisterService(fuchsia::audio::DecoderCreator::Name_,
                                      std::make_unique<FakeAudioDecoderCreator::Binder>());
    service_provider_.RegisterService(fuchsia::video::DecoderCreator::Name_,
                                      std::make_unique<FakeVideoDecoderCreator::Binder>());
  }

  Thread& thread() { return thread_; }

  ServiceProvider& service_provider() { return service_provider_; }

  // fuchsia::media2::BufferProvider implementation.
  void CreateBufferCollection(zx::eventpair provider_token, std::string vmo_name,
                              CreateBufferCollectionCallback callback) override {
    provider_token_ = std::move(provider_token);
    fuchsia::media2::BufferCollectionInfo response;
    response.set_buffer_count(1);
    response.set_buffer_size(1);
    callback(fpromise::ok(std::move(response)));
  }

  void GetBuffers(zx::eventpair participant_token, fuchsia::media2::BufferConstraints constraints,
                  fuchsia::media2::BufferRights rights, std::string name, uint64_t id,
                  GetBuffersCallback callback) override {
    EXPECT_TRUE(false) << "Unexpected call to GetBuffers";
  }

  void BindSysmemToken(zx::eventpair participant_token, BindSysmemTokenCallback callback) override {
    EXPECT_TRUE(false) << "Unexpected call to BindSysmemToken";
  }

 private:
  Thread thread_;
  ServiceProvider service_provider_;
  zx::eventpair provider_token_;
};

const fuchsia::mediastreams::AudioFormat kAudioFormat{
    .sample_format = fuchsia::mediastreams::AudioSampleFormat::SIGNED_16,
    .channel_count = 2,
    .frames_per_second = 48000,
    .channel_layout = fuchsia::mediastreams::AudioChannelLayout::WithPlaceholder(0)};
const fuchsia::mediastreams::VideoFormat kVideoFormat{
    .pixel_format = fuchsia::mediastreams::PixelFormat::NV12,
    .pixel_format_modifier = 0,
    .color_space = fuchsia::mediastreams::ColorSpace::REC709,
    .coded_size = fuchsia::math::Size{.width = 640, .height = 480},
    .display_size = fuchsia::math::Size{.width = 640, .height = 480},
    .aspect_ratio = nullptr};
const std::unique_ptr<std::string> kOpusCompressionType =
    std::make_unique<std::string>(fuchsia::mediastreams::AUDIO_COMPRESSION_OPUS);
const std::unique_ptr<std::string> kH264CompressionType =
    std::make_unique<std::string>(fuchsia::mediastreams::VIDEO_COMPRESSION_H264);
constexpr int64_t kPacketTimestampInterval = 1234;
constexpr int64_t kPresentationInterval = 5678;

// Tests that |Connect| properly connects a |fuchsia::audiovideo::ProducerStream| to a
// |fuchsia::audio::Consumer|.
TEST_F(ConnectorTest, ProducerStreamToAudioConsumer) {
  fuchsia::audiovideo::ProducerStreamPtr producer_ptr;
  FakeAvProducerStream producer(producer_ptr.NewRequest());
  fuchsia::audio::ConsumerPtr consumer_ptr;
  FakeAudioConsumer consumer(consumer_ptr.NewRequest());

  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kOpusCompressionType;
  auto timestamp_units = fuchsia::media2::PacketTimestampUnits::New();
  timestamp_units->packet_timestamp_interval = kPacketTimestampInterval;
  timestamp_units->presentation_interval = kPresentationInterval;
  bool task_completed = false;
  thread().schedule_task(
      Connect(producer_ptr, consumer_ptr, kAudioFormat, compression, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(producer.output_stream_connected());
  EXPECT_TRUE(consumer.input_stream_connected());

  auto& out = producer.output_stream_parameters();
  auto& in = consumer.input_stream_parameters();

  EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

  EXPECT_FALSE(out.format_.has_value());  // ProducerStream doesn't accept format.
  EXPECT_TRUE(in.format_.has_value());
  EXPECT_EQ(kAudioFormat, in.format_.value());

  EXPECT_FALSE(!!out.compression_);  // ProducerStream doesn't accept compression.
  EXPECT_TRUE(!!in.compression_);
  EXPECT_EQ(*kOpusCompressionType, in.compression_->type);

  EXPECT_FALSE(!!out.timestamp_units_);  // ProducerStream doesn't accept timestamp_units.
  EXPECT_TRUE(!!in.timestamp_units_);
  EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
  EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

  EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
}

// Tests that |Connect| properly connects a |fuchsia::audiovideo::ProducerStream| to a
// |fuchsia::video::Consumer|.
TEST_F(ConnectorTest, ProducerStreamToVideoConsumer) {
  fuchsia::audiovideo::ProducerStreamPtr producer_ptr;
  FakeAvProducerStream producer(producer_ptr.NewRequest());
  fuchsia::video::ConsumerPtr consumer_ptr;
  FakeVideoConsumer consumer(consumer_ptr.NewRequest());

  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kH264CompressionType;
  auto timestamp_units = fuchsia::media2::PacketTimestampUnits::New();
  timestamp_units->packet_timestamp_interval = kPacketTimestampInterval;
  timestamp_units->presentation_interval = kPresentationInterval;
  bool task_completed = false;
  thread().schedule_task(
      Connect(producer_ptr, consumer_ptr, kVideoFormat, compression, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(producer.output_stream_connected());
  EXPECT_TRUE(consumer.input_stream_connected());

  auto& out = producer.output_stream_parameters();
  auto& in = consumer.input_stream_parameters();

  EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

  EXPECT_FALSE(out.format_.has_value());  // ProducerStream doesn't accept format.
  EXPECT_TRUE(in.format_.has_value());
  EXPECT_EQ(kVideoFormat, in.format_.value());

  EXPECT_FALSE(!!out.compression_);  // ProducerStream doesn't accept compression.
  EXPECT_TRUE(!!in.compression_);
  EXPECT_EQ(*kH264CompressionType, in.compression_->type);

  EXPECT_FALSE(!!out.timestamp_units_);  // ProducerStream doesn't accept timestamp_units.
  EXPECT_TRUE(!!in.timestamp_units_);
  EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
  EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

  EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
}

// Tests that |Connect| properly connects a |fuchsia::audio::Producer| to a
// |fuchsia::audio::Consumer|.
TEST_F(ConnectorTest, AudioProducerToAudioConsumer) {
  fuchsia::audio::ProducerPtr producer_ptr;
  FakeAudioProducer producer(producer_ptr.NewRequest());
  fuchsia::audio::ConsumerPtr consumer_ptr;
  FakeAudioConsumer consumer(consumer_ptr.NewRequest());

  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kOpusCompressionType;
  auto timestamp_units = fuchsia::media2::PacketTimestampUnits::New();
  timestamp_units->packet_timestamp_interval = kPacketTimestampInterval;
  timestamp_units->presentation_interval = kPresentationInterval;
  bool task_completed = false;

  // Audio producers currently don't support compression, so we pass nullptr here.
  thread().schedule_task(
      Connect(producer_ptr, consumer_ptr, kAudioFormat, nullptr, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(producer.output_stream_connected());
  EXPECT_TRUE(consumer.input_stream_connected());

  auto& out = producer.output_stream_parameters();
  auto& in = consumer.input_stream_parameters();

  EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

  EXPECT_TRUE(out.format_.has_value());
  EXPECT_EQ(kAudioFormat, out.format_.value());
  EXPECT_TRUE(in.format_.has_value());
  EXPECT_EQ(kAudioFormat, in.format_.value());

  EXPECT_FALSE(!!out.compression_);
  EXPECT_FALSE(!!in.compression_);

  EXPECT_TRUE(!!out.timestamp_units_);
  EXPECT_EQ(kPacketTimestampInterval, out.timestamp_units_->packet_timestamp_interval);
  EXPECT_EQ(kPresentationInterval, out.timestamp_units_->presentation_interval);
  EXPECT_TRUE(!!in.timestamp_units_);
  EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
  EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

  EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
}

// Tests that |Connect| properly connects a |fuchsia::audiovideo::ProducerStream| to an
// |AudioConversionPipeline|, and that |Connect| properly connects the |AudioConversionPipeline| to
// a |fuchsia::audio::Consumer|.
TEST_F(ConnectorTest, ProducerStreamToAudioConversionPipelineToAudioConsumer) {
  fuchsia::audiovideo::ProducerStreamPtr producer_ptr;
  FakeAvProducerStream producer(producer_ptr.NewRequest());
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kOpusCompressionType;
  auto pipeline = AudioConversionPipeline::Create(kAudioFormat, fidl::Clone(compression), nullptr,
                                                  service_provider());
  EXPECT_TRUE(!!pipeline);

  auto timestamp_units = fuchsia::media2::PacketTimestampUnits::New();
  timestamp_units->packet_timestamp_interval = kPacketTimestampInterval;
  timestamp_units->presentation_interval = kPresentationInterval;
  bool task_completed = false;
  thread().schedule_task(
      Connect(producer_ptr, *pipeline, kAudioFormat, compression, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(FakeAudioDecoder::instance());
  FakeAudioDecoder& decoder = *FakeAudioDecoder::instance();
  EXPECT_EQ(kAudioFormat, decoder.format());
  EXPECT_EQ(*kOpusCompressionType, decoder.compression().type);

  EXPECT_TRUE(producer.output_stream_connected());
  EXPECT_TRUE(decoder.input_stream_connected());

  {
    auto& out = producer.output_stream_parameters();
    auto& in = decoder.input_stream_parameters();

    EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

    EXPECT_FALSE(out.format_.has_value());  // ProducerStream doesn't accept format.
    EXPECT_FALSE(in.format_.has_value());   // Decoder doesn't accept format.

    EXPECT_FALSE(!!out.compression_);  // ProducerStream doesn't accept compression.
    EXPECT_FALSE(!!in.compression_);   // Decoder doesn't accept compression.

    EXPECT_FALSE(!!out.timestamp_units_);  // ProducerStream doesn't accept timestamp_units.
    EXPECT_TRUE(!!in.timestamp_units_);
    EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
    EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

    EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
  }

  fuchsia::audio::ConsumerPtr consumer_ptr;
  FakeAudioConsumer consumer(consumer_ptr.NewRequest());

  task_completed = false;
  thread().schedule_task(
      Connect(*pipeline, consumer_ptr, kAudioFormat, nullptr, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(decoder.output_stream_connected());
  EXPECT_TRUE(consumer.input_stream_connected());

  auto& out = decoder.output_stream_parameters();
  auto& in = consumer.input_stream_parameters();

  EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

  EXPECT_FALSE(out.format_.has_value());  // Pipeline output doesn't accept format.
  EXPECT_TRUE(in.format_.has_value());
  EXPECT_EQ(kAudioFormat, in.format_.value());

  EXPECT_FALSE(!!out.compression_);  // Pipeline output doesn't accept compression.
  EXPECT_FALSE(!!in.compression_);   // The stream is uncompressed.

  EXPECT_FALSE(!!out.timestamp_units_);  // Pipeline output doesn't accept timestamp units.
  EXPECT_TRUE(!!in.timestamp_units_);
  EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
  EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

  EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
}

// Tests that |Connect| properly connects a |fuchsia::audiovideo::ProducerStream| to an
// |VideoConversionPipeline|, and that |Connect| properly connects the |VideoConversionPipeline| to
// a |fuchsia::video::Consumer|.
TEST_F(ConnectorTest, ProducerStreamToVideoConversionPipelineToVideoConsumer) {
  fuchsia::audiovideo::ProducerStreamPtr producer_ptr;
  FakeAvProducerStream producer(producer_ptr.NewRequest());
  auto compression = fuchsia::mediastreams::Compression::New();
  compression->type = *kH264CompressionType;
  auto pipeline = VideoConversionPipeline::Create(kVideoFormat, fidl::Clone(compression), nullptr,
                                                  service_provider());
  EXPECT_TRUE(!!pipeline);

  auto timestamp_units = fuchsia::media2::PacketTimestampUnits::New();
  timestamp_units->packet_timestamp_interval = kPacketTimestampInterval;
  timestamp_units->presentation_interval = kPresentationInterval;
  bool task_completed = false;
  thread().schedule_task(
      Connect(producer_ptr, *pipeline, kVideoFormat, compression, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(FakeVideoDecoder::instance());
  FakeVideoDecoder& decoder = *FakeVideoDecoder::instance();
  EXPECT_EQ(kVideoFormat, decoder.format());
  EXPECT_EQ(*kH264CompressionType, decoder.compression().type);

  EXPECT_TRUE(producer.output_stream_connected());
  EXPECT_TRUE(decoder.input_stream_connected());

  {
    auto& out = producer.output_stream_parameters();
    auto& in = decoder.input_stream_parameters();

    EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

    EXPECT_FALSE(out.format_.has_value());  // ProducerStream doesn't accept format.
    EXPECT_FALSE(in.format_.has_value());   // Decoder doesn't accept format.

    EXPECT_FALSE(!!out.compression_);  // ProducerStream doesn't accept compression.
    EXPECT_FALSE(!!in.compression_);   // Decoder doesn't accept compression.

    EXPECT_FALSE(!!out.timestamp_units_);  // ProducerStream doesn't accept timestamp_units.
    EXPECT_TRUE(!!in.timestamp_units_);
    EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
    EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

    EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
  }

  fuchsia::video::ConsumerPtr consumer_ptr;
  FakeVideoConsumer consumer(consumer_ptr.NewRequest());

  task_completed = false;
  thread().schedule_task(
      Connect(*pipeline, consumer_ptr, kVideoFormat, nullptr, timestamp_units, *this)
          .then(
              [&task_completed](fpromise::result<void, fuchsia::media2::ConnectionError>& result) {
                task_completed = true;
                EXPECT_TRUE(result.is_ok());
              }));

  RunLoopUntilIdle();
  EXPECT_TRUE(task_completed);

  EXPECT_TRUE(decoder.output_stream_connected());
  EXPECT_TRUE(consumer.input_stream_connected());

  auto& out = decoder.output_stream_parameters();
  auto& in = consumer.input_stream_parameters();

  EXPECT_EQ(GetKoid(out.buffer_collection_token_), GetKoid(in.buffer_collection_token_));

  EXPECT_FALSE(out.format_.has_value());  // Pipeline output doesn't accept format.
  EXPECT_TRUE(in.format_.has_value());
  EXPECT_EQ(kVideoFormat, in.format_.value());

  EXPECT_FALSE(!!out.compression_);  // Pipeline output doesn't accept compression.
  EXPECT_FALSE(!!in.compression_);   // The stream is uncompressed.

  EXPECT_FALSE(!!out.timestamp_units_);  // Pipeline output doesn't accept timestamp units.
  EXPECT_TRUE(!!in.timestamp_units_);
  EXPECT_EQ(kPacketTimestampInterval, in.timestamp_units_->packet_timestamp_interval);
  EXPECT_EQ(kPresentationInterval, in.timestamp_units_->presentation_interval);

  EXPECT_EQ(GetPeerKoid(out.handle_.channel()), GetKoid(in.request_.channel()));
}

}  // namespace
}  // namespace fmlib
