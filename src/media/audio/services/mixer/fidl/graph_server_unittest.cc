// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_server.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/common/testing/test_server_and_client.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/fidl/clock_registry.h"
#include "src/media/audio/services/mixer/fidl/real_clock_factory.h"
#include "src/media/audio/services/mixer/fidl/synthetic_clock_server.h"

namespace media_audio {
namespace {

const Format kFormat = Format::CreateOrDie({
    .sample_format = ::fuchsia_mediastreams::wire::AudioSampleFormat::kFloat,
    .channel_count = 2,
    .frames_per_second = 48000,
});

const auto kInvalidFormatFidl = fuchsia_mediastreams::wire::AudioFormat{
    .sample_format = ::fuchsia_mediastreams::wire::AudioSampleFormat::kFloat,
    .channel_count = 0,  // illegal
    .frames_per_second = 48000,
    .channel_layout = fuchsia_mediastreams::wire::AudioChannelLayout::WithPlaceholder(0),
};

zx::clock MakeClock() {
  zx::clock clock;
  if (auto status = zx::clock::create(
          ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS, nullptr,
          &clock);
      status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "clock.create failed";
  }
  return clock;
}

fuchsia_audio_mixer::wire::ReferenceClock MakeReferenceClock(fidl::AnyArena& arena) {
  return fuchsia_audio_mixer::wire::ReferenceClock::Builder(arena).handle(MakeClock()).Build();
}

zx::vmo MakeVmo(size_t size = 1024) {
  zx::vmo vmo;
  if (auto status = zx::vmo::create(size, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::vmo::create failed";
  }
  return vmo;
}

zx::vmo MakeInvalidVmo(size_t size = 1024) {
  zx::vmo vmo;
  if (auto status = zx::vmo::create(size, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::vmo::create failed";
  }
  // Remove ZX_RIGHT_MAP.
  if (auto status = vmo.replace(
          ZX_RIGHT_READ | ZX_RIGHT_WRITE | ZX_RIGHT_TRANSFER | ZX_RIGHT_GET_PROPERTY, &vmo);
      status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::vmo::replace failed";
  }
  return vmo;
}

fidl::WireTableBuilder<fuchsia_audio_mixer::wire::StreamSinkProducer> MakeDefaultStreamSinkProducer(
    fidl::AnyArena& arena) {
  auto [stream_sink_client, stream_sink_server] = CreateClientOrDie<fuchsia_media2::StreamSink>();
  auto builder = fuchsia_audio_mixer::wire::StreamSinkProducer::Builder(arena);
  builder.server_end(std::move(stream_sink_server));
  builder.format(kFormat.ToFidl());
  builder.reference_clock(MakeReferenceClock(arena));
  builder.payload_buffer(MakeVmo());
  builder.media_ticks_per_second_numerator(1);
  builder.media_ticks_per_second_denominator(1);
  return builder;
}

fidl::WireTableBuilder<fuchsia_audio_mixer::wire::StreamSinkConsumer> MakeDefaultStreamSinkConsumer(
    fidl::AnyArena& arena) {
  auto [stream_sink_client, stream_sink_server] = CreateClientOrDie<fuchsia_media2::StreamSink>();
  auto builder = fuchsia_audio_mixer::wire::StreamSinkConsumer::Builder(arena);
  builder.client_end(stream_sink_client.TakeClientEnd());
  builder.format(kFormat.ToFidl());
  builder.reference_clock(MakeReferenceClock(arena));
  builder.payload_buffer(MakeVmo());
  builder.media_ticks_per_second_numerator(1);
  builder.media_ticks_per_second_denominator(1);
  return builder;
}

fidl::WireTableBuilder<fuchsia_audio::wire::RingBuffer> MakeDefaultRingBuffer(
    fidl::AnyArena& arena) {
  auto builder = fuchsia_audio::wire::RingBuffer::Builder(arena);
  builder.vmo(MakeVmo(1024));
  builder.format(kFormat.ToFidl());
  builder.producer_bytes(512);
  builder.consumer_bytes(512);
  builder.reference_clock(MakeClock());
  return builder;
}

fidl::WireTableBuilder<fuchsia_audio_mixer::wire::GraphCreateThreadRequest>
MakeDefaultCreateThreadRequest(fidl::AnyArena& arena) {
  auto builder = fuchsia_audio_mixer::wire::GraphCreateThreadRequest::Builder(arena);
  builder.name(fidl::StringView::FromExternal("thread"));
  builder.period(zx::msec(10).to_nsecs());
  builder.cpu_per_period(zx::msec(5).to_nsecs());
  return builder;
}

// Testing strategy: we test all error cases implemented in graph_server.cc and very high-level
// success cases. We leave graph behavior testing (e.g. mixing) for integration tests.
class GraphServerTest : public ::testing::Test {
 public:
  GraphServer& server() { return wrapper_.server(); }
  fidl::WireSyncClient<fuchsia_audio_mixer::Graph>& client() { return wrapper_.client(); }

 protected:
  fidl::Arena<> arena_;

 private:
  std::shared_ptr<FidlThread> thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
  std::shared_ptr<FidlThread> realtime_thread_ =
      FidlThread::CreateFromNewThread("test_realtime_fidl_thread");
  TestServerAndClient<GraphServer> wrapper_{
      thread_, GraphServer::Args{
                   .realtime_fidl_thread = realtime_thread_,
                   .clock_factory = std::make_shared<RealClockFactory>(),
                   .clock_registry = std::make_shared<ClockRegistry>(),
               }};
};

//
// CreateProducer
//

TEST_F(GraphServerTest, CreateProducerFailsMissingDirection) {
  auto [stream_sink_client, stream_sink_server] = CreateClientOrDie<fuchsia_media2::StreamSink>();

  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          // no direction()
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithStreamSink(
              arena_, MakeDefaultStreamSinkProducer(arena_).Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
}

TEST_F(GraphServerTest, CreateProducerFailsMissingDataSource) {
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          // no data_source()
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
}

TEST_F(GraphServerTest, CreateProducerFailsUnknownDataSource) {
  using ProducerDataSource = ::fuchsia_audio_mixer::wire::ProducerDataSource;

  // TODO(fxbug.dev/109467): When this bug is fixed, this code can be simplified. For now we need to
  // create an "unknown" variant manually.
  struct RawProducerDataSource {
    fidl_xunion_tag_t ordinal;
    FIDL_ALIGNDECL fidl::UntypedEnvelope envelope;
  };
  // C++ type punning requires using std::memcpy.
  RawProducerDataSource raw{
      .ordinal = static_cast<fidl_xunion_tag_t>(ProducerDataSource::Tag::kUnknown),
  };
  ProducerDataSource data_source;
  static_assert(sizeof(data_source) == sizeof(raw));
  std::memcpy(&data_source, &raw, sizeof(data_source));

  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fidl::ObjectView<ProducerDataSource>::FromExternal(&data_source))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kUnsupportedOption);
}

TEST_F(GraphServerTest, CreateProducerStreamSinkFailsBadFields) {
  using CreateNodeError = fuchsia_audio_mixer::CreateNodeError;

  struct TestCase {
    std::string name;
    std::function<void(fidl::WireTableBuilder<fuchsia_audio_mixer::wire::StreamSinkProducer>&)>
        edit;
    CreateNodeError expected_error;
  };
  std::vector<TestCase> cases = {
      {
          .name = "MissingFormat",
          .edit =
              [](auto data_source) {
                data_source.format(fidl::ObjectView<fuchsia_mediastreams::wire::AudioFormat>());
              },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "MissingReferenceClock",
          .edit =
              [](auto data_source) {
                data_source.reference_clock(
                    fidl::ObjectView<fuchsia_audio_mixer::wire::ReferenceClock>());
              },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "MissingReferenceClockHandle",
          .edit =
              [this](auto data_source) {
                data_source.reference_clock(
                    fuchsia_audio_mixer::wire::ReferenceClock::Builder(arena_).Build());
              },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "MissingTicksPerSecondNumerator",
          .edit =
              [](auto data_source) {
                data_source.media_ticks_per_second_numerator(fidl::ObjectView<uint64_t>());
              },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "MissingTicksPerSecondDenominator",
          .edit =
              [](auto data_source) {
                data_source.media_ticks_per_second_denominator(fidl::ObjectView<uint64_t>());
              },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "InvalidFormat",
          .edit = [](auto data_source) { data_source.format(kInvalidFormatFidl); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "InvalidPayloadBuffer",
          .edit = [](auto data_source) { data_source.payload_buffer(MakeInvalidVmo()); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "InvalidMediaTicksPerSecondNumerator",
          .edit = [](auto data_source) { data_source.media_ticks_per_second_numerator(0); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "InvalidMediaTicksPerSecondDenominator",
          .edit = [](auto data_source) { data_source.media_ticks_per_second_denominator(0); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
  };

  for (auto& tc : cases) {
    SCOPED_TRACE("TestCase: " + tc.name);
    auto data_source = MakeDefaultStreamSinkProducer(arena_);
    tc.edit(data_source);

    auto result = client()->CreateProducer(
        fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("producer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithStreamSink(
                arena_, data_source.Build()))
            .Build());

    if (!result.ok()) {
      ADD_FAILURE() << "failed to send method call: " << result;
      continue;
    }
    if (!result->is_error()) {
      ADD_FAILURE() << "CreateProducer did not fail";
      continue;
    }
    EXPECT_EQ(result->error_value(), tc.expected_error);
  }
}

// TODO(fxbug.dev/109458): can be merged into CreateProducerStreamSinkFailsBadFields after fix.
TEST_F(GraphServerTest, CreateProducerStreamSinkFailsMissingServerEnd) {
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithStreamSink(
              arena_, fuchsia_audio_mixer::wire::StreamSinkProducer::Builder(arena_)
                          // no server_end()
                          .format(kFormat.ToFidl())
                          .reference_clock(MakeReferenceClock(arena_))
                          .payload_buffer(MakeVmo())
                          .media_ticks_per_second_numerator(1)
                          .media_ticks_per_second_denominator(1)
                          .Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
}

// TODO(fxbug.dev/109458): can be merged into CreateProducerStreamSinkFailsBadFields after fix.
TEST_F(GraphServerTest, CreateProducerStreamSinkFailsMissingPayloadBuffer) {
  auto [stream_sink_client, stream_sink_server] = CreateClientOrDie<fuchsia_media2::StreamSink>();
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithStreamSink(
              arena_, fuchsia_audio_mixer::wire::StreamSinkProducer::Builder(arena_)
                          .server_end(std::move(stream_sink_server))
                          .format(kFormat.ToFidl())
                          .reference_clock(MakeReferenceClock(arena_))
                          // no payload_buffer()
                          .media_ticks_per_second_numerator(1)
                          .media_ticks_per_second_denominator(1)
                          .Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
}

TEST_F(GraphServerTest, CreateProducerStreamSinkSuccess) {
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithStreamSink(
              arena_, MakeDefaultStreamSinkProducer(arena_).Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result->is_error()) << result->error_value();
  ASSERT_TRUE(result->value()->has_id());
  EXPECT_EQ(result->value()->id(), 1u);
}

TEST_F(GraphServerTest, CreateProducerRingBufferFailsBadFields) {
  using CreateNodeError = fuchsia_audio_mixer::CreateNodeError;

  struct TestCase {
    std::string name;
    std::function<void(fidl::WireTableBuilder<fuchsia_audio::wire::RingBuffer>&)> edit;
    CreateNodeError expected_error;
  };
  std::vector<TestCase> cases = {
      {
          .name = "MissingFormat",
          .edit =
              [](auto ring_buffer) {
                ring_buffer.format(fidl::ObjectView<fuchsia_mediastreams::wire::AudioFormat>());
              },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "MissingProducerBytes",
          .edit =
              [](auto ring_buffer) { ring_buffer.producer_bytes(fidl::ObjectView<uint64_t>()); },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "MissingConsumerBytes",
          .edit =
              [](auto ring_buffer) { ring_buffer.consumer_bytes(fidl::ObjectView<uint64_t>()); },
          .expected_error = CreateNodeError::kMissingRequiredField,
      },
      {
          .name = "InvalidFormat",
          .edit = [](auto ring_buffer) { ring_buffer.format(kInvalidFormatFidl); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "InvalidVmo",
          .edit = [](auto ring_buffer) { ring_buffer.vmo(MakeInvalidVmo()); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "ProducerBytesSpansNonIntergalFrames",
          .edit =
              [](auto ring_buffer) { ring_buffer.producer_bytes(kFormat.bytes_per_frame() + 1); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "ConsumerBytesSpansNonIntergalFrames",
          .edit =
              [](auto ring_buffer) { ring_buffer.consumer_bytes(kFormat.bytes_per_frame() + 1); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "ProducerPlusConsumerBytesTooBig",
          .edit =
              [](auto ring_buffer) {
                ring_buffer.vmo(MakeVmo(1024));
                ring_buffer.producer_bytes(512);
                ring_buffer.consumer_bytes(513);
              },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
      {
          .name = "VmoTooSmall",
          .edit = [](auto ring_buffer) { ring_buffer.vmo(MakeVmo(kFormat.bytes_per_frame() - 1)); },
          .expected_error = CreateNodeError::kInvalidParameter,
      },
  };

  for (auto& tc : cases) {
    SCOPED_TRACE("TestCase: " + tc.name);
    auto ring_buffer = MakeDefaultRingBuffer(arena_);
    tc.edit(ring_buffer);

    auto result = client()->CreateProducer(
        fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("producer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithRingBuffer(
                arena_, ring_buffer.Build()))
            .Build());

    if (!result.ok()) {
      ADD_FAILURE() << "failed to send method call: " << result;
      continue;
    }
    if (!result->is_error()) {
      ADD_FAILURE() << "CreateProducer did not fail";
      continue;
    }
    EXPECT_EQ(result->error_value(), tc.expected_error);
  }
}

// TODO(fxbug.dev/109458): can be merged into CreateProducerRingBufferFailsBadFields after fix.
TEST_F(GraphServerTest, CreateProducerRingBufferFailsMissingVmo) {
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithRingBuffer(
              arena_, fuchsia_audio::wire::RingBuffer::Builder(arena_)
                          // no vmo()
                          .format(kFormat.ToFidl())
                          .producer_bytes(512)
                          .consumer_bytes(512)
                          .reference_clock(MakeClock())
                          .Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
}

// TODO(fxbug.dev/109458): can be merged into CreateProducerRingBufferFailsBadFields after fix.
TEST_F(GraphServerTest, CreateProducerRingBufferFailsMissingReferenceClock) {
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithRingBuffer(
              arena_, fuchsia_audio::wire::RingBuffer::Builder(arena_)
                          .vmo(MakeVmo(1024))
                          .format(kFormat.ToFidl())
                          .producer_bytes(512)
                          .consumer_bytes(512)
                          // no reference_clock()
                          .Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
}

TEST_F(GraphServerTest, CreateProducerRingBufferSuccess) {
  auto result = client()->CreateProducer(
      fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("producer"))
          .direction(PipelineDirection::kOutput)
          .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithRingBuffer(
              arena_, MakeDefaultRingBuffer(arena_).Build()))
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result->is_error()) << result->error_value();
  ASSERT_TRUE(result->value()->has_id());
  EXPECT_EQ(result->value()->id(), 1u);
}

//
// CreateConsumer
//
// Since CreateProducer and CreateConsumer share most of the same validation code, CreateConsumer's
// "BadFields" tests are mostly covered by tests above. We don't bother repeating those cases here.
//

TEST_F(GraphServerTest, CreateConsumerFailsBadFields) {
  // Each consumer needs a thread.
  {
    auto result = client()->CreateThread(MakeDefaultCreateThreadRequest(arena_).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }

  // TODO(fxbug.dev/109458): can be switch to a table-driven test after fix.
  {
    SCOPED_TRACE("MissingDirection");

    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            // no direction()
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithRingBuffer(
                arena_, MakeDefaultRingBuffer(arena_).Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_).thread(1).Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  {
    SCOPED_TRACE("MissingDataSource");

    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            // no data_source()
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_).thread(1).Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  {
    SCOPED_TRACE("MissingStreamSinkClientEnd");

    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithStreamSink(
                arena_, fuchsia_audio_mixer::wire::StreamSinkConsumer::Builder(arena_)
                            // no client_end()
                            .format(kFormat.ToFidl())
                            .reference_clock(MakeReferenceClock(arena_))
                            .payload_buffer(MakeVmo())
                            .media_ticks_per_second_numerator(1)
                            .media_ticks_per_second_denominator(1)
                            .Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_).thread(1).Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  {
    SCOPED_TRACE("MissingOptions");

    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithRingBuffer(
                arena_, MakeDefaultRingBuffer(arena_).Build()))
            // no options()
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  {
    SCOPED_TRACE("MissingThread");

    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithRingBuffer(
                arena_, MakeDefaultRingBuffer(arena_).Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_)
                         // no thread()
                         .Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kMissingRequiredField);
  }

  {
    SCOPED_TRACE("UnknownThread");

    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithRingBuffer(
                arena_, MakeDefaultRingBuffer(arena_).Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_)
                         .thread(2)  // non-existent thread
                         .Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::CreateNodeError::kInvalidParameter);
  }
}

TEST_F(GraphServerTest, CreateConsumerStreamSinkSuccess) {
  // Each consumer needs a thread.
  {
    auto result = client()->CreateThread(MakeDefaultCreateThreadRequest(arena_).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }

  {
    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithStreamSink(
                arena_, MakeDefaultStreamSinkConsumer(arena_).Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_).thread(1).Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }
}

TEST_F(GraphServerTest, CreateConsumerRingBufferSuccess) {
  // Each consumer needs a thread.
  {
    auto result = client()->CreateThread(MakeDefaultCreateThreadRequest(arena_).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }

  {
    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithRingBuffer(
                arena_, MakeDefaultRingBuffer(arena_).Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_).thread(1).Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }
}

//
// CreateThread
//

TEST_F(GraphServerTest, CreateThreadFailsBadFields) {
  using CreateThreadError = fuchsia_audio_mixer::CreateThreadError;

  struct TestCase {
    std::string name;
    std::function<void(
        fidl::WireTableBuilder<fuchsia_audio_mixer::wire::GraphCreateThreadRequest>&)>
        edit;
    CreateThreadError expected_error;
  };
  std::vector<TestCase> cases = {
      {
          .name = "MissingPeriod",
          .edit = [](auto request) { request.period(fidl::ObjectView<int64_t>()); },
          .expected_error = CreateThreadError::kMissingRequiredField,
      },
      {
          .name = "MissingCpuPerPeriod",
          .edit = [](auto request) { request.cpu_per_period(fidl::ObjectView<int64_t>()); },
          .expected_error = CreateThreadError::kMissingRequiredField,
      },
      {
          .name = "ZeroPeriod",
          .edit = [](auto request) { request.period(0); },
          .expected_error = CreateThreadError::kInvalidParameter,
      },
      {
          .name = "ZeroCpuPerPeriod",
          .edit = [](auto request) { request.cpu_per_period(0); },
          .expected_error = CreateThreadError::kInvalidParameter,
      },
      {
          .name = "NegativePeriod",
          .edit = [](auto request) { request.period(-1); },
          .expected_error = CreateThreadError::kInvalidParameter,
      },
      {
          .name = "NegativeCpuPerPeriod",
          .edit = [](auto request) { request.cpu_per_period(-1); },
          .expected_error = CreateThreadError::kInvalidParameter,
      },
      {
          .name = "CpuPerPeriodTooBig",
          .edit =
              [](auto request) {
                request.period(10);
                request.cpu_per_period(11);
              },
          .expected_error = CreateThreadError::kInvalidParameter,
      },
  };

  for (auto& tc : cases) {
    SCOPED_TRACE("TestCase: " + tc.name);
    auto request = MakeDefaultCreateThreadRequest(arena_);
    tc.edit(request);

    auto result = client()->CreateThread(request.Build());
    if (!result.ok()) {
      ADD_FAILURE() << "failed to send method call: " << result;
      continue;
    }
    if (!result->is_error()) {
      ADD_FAILURE() << "CreateThread did not fail";
      continue;
    }
    EXPECT_EQ(result->error_value(), tc.expected_error);
  }
}

TEST_F(GraphServerTest, CreateThreadSuccess) {
  auto result = client()->CreateThread(MakeDefaultCreateThreadRequest(arena_).Build());
  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result->is_error()) << result->error_value();
  ASSERT_TRUE(result->value()->has_id());
  EXPECT_EQ(result->value()->id(), 1u);
}

//
// DeleteThread
//

TEST_F(GraphServerTest, DeleteThreadFailsMissingId) {
  auto result = client()->DeleteThread(
      fuchsia_audio_mixer::wire::GraphDeleteThreadRequest::Builder(arena_).Build());
  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::DeleteThreadError::kInvalidId);
}

TEST_F(GraphServerTest, DeleteThreadFailsIdNotFound) {
  auto result = client()->DeleteThread(
      fuchsia_audio_mixer::wire::GraphDeleteThreadRequest::Builder(arena_).id(1).Build());
  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::DeleteThreadError::kInvalidId);
}

TEST_F(GraphServerTest, DeleteThreadFailsStillInUse) {
  // Create a thread.
  {
    auto result = client()->CreateThread(MakeDefaultCreateThreadRequest(arena_).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }

  // Attach a consumer.
  {
    auto result = client()->CreateConsumer(
        fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("consumer"))
            .direction(PipelineDirection::kOutput)
            .data_source(fuchsia_audio_mixer::wire::ConsumerDataSource::WithRingBuffer(
                arena_, MakeDefaultRingBuffer(arena_).Build()))
            .options(fuchsia_audio_mixer::wire::ConsumerOptions::Builder(arena_).thread(1).Build())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // Delete should fail.
  {
    auto result = client()->DeleteThread(
        fuchsia_audio_mixer::wire::GraphDeleteThreadRequest::Builder(arena_).id(1).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    ASSERT_EQ(result->error_value(), fuchsia_audio_mixer::DeleteThreadError::kStillInUse);
  }
}

TEST_F(GraphServerTest, DeleteThreadSuccess) {
  // Create a thread.
  {
    auto result = client()->CreateThread(MakeDefaultCreateThreadRequest(arena_).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    ASSERT_TRUE(result->value()->has_id());
    EXPECT_EQ(result->value()->id(), 1u);
  }

  // Delete it.
  {
    auto result = client()->DeleteThread(
        fuchsia_audio_mixer::wire::GraphDeleteThreadRequest::Builder(arena_).id(1).Build());
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }
}

// TODO(fxbug.dev/87651): after implementing DeleteNode, add a case where a consumer is created on a
// thread then deleted

}  // namespace
}  // namespace media_audio
