// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/link_matrix.h"

#include <lib/gtest/test_loop_fixture.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/volume_curve.h"
#include "src/media/audio/lib/clock/clone_mono.h"

using testing::IsEmpty;
using testing::UnorderedElementsAreArray;

namespace media::audio {
namespace {

class MockObject : public AudioObject {
 public:
  explicit MockObject(AudioObject::Type object_type) : AudioObject(object_type) {}

  fit::result<std::pair<std::shared_ptr<Mixer>, ExecutionDomain*>, zx_status_t>
  InitializeSourceLink(const AudioObject& source, std::shared_ptr<ReadableStream> stream) override {
    source_ = &source;
    stream_ = stream;
    return fit::ok(std::make_pair(mixer_, nullptr));
  }
  fit::result<std::shared_ptr<ReadableStream>, zx_status_t> InitializeDestLink(
      const AudioObject& dest) override {
    dest_ = &dest;
    return fit::ok(stream_);
  }

  void CleanupSourceLink(const AudioObject& source,
                         std::shared_ptr<ReadableStream> stream) override {
    cleaned_source_stream_ = stream;
    cleaned_source_link_ = &source;
  }

  void CleanupDestLink(const AudioObject& dest) override { cleaned_dest_link_ = &dest; }

  void OnLinkAdded() override { on_link_added_called_ = true; }

  std::shared_ptr<ReadableStream> cleaned_source_stream() const { return cleaned_source_stream_; }

  const AudioObject* cleaned_source_link() const { return cleaned_source_link_; }

  const AudioObject* cleaned_dest_link() const { return cleaned_dest_link_; }

  bool on_link_added_called() const { return on_link_added_called_; }

  void set_stream(std::shared_ptr<ReadableStream> stream) { stream_ = stream; }

  void set_mixer(std::shared_ptr<Mixer> mixer) { mixer_ = std::move(mixer); }

  const AudioObject* source() const { return source_; }

  std::shared_ptr<ReadableStream> stream() const { return stream_; }

  const AudioObject* dest() const { return dest_; }

 private:
  const AudioObject* source_ = nullptr;
  std::shared_ptr<ReadableStream> stream_;
  const AudioObject* dest_ = nullptr;
  std::shared_ptr<Mixer> mixer_;

  bool on_link_added_called_ = false;

  std::shared_ptr<ReadableStream> cleaned_source_stream_;
  const AudioObject* cleaned_source_link_ = nullptr;
  const AudioObject* cleaned_dest_link_ = nullptr;
};

class FakeLoudnessTransform : public LoudnessTransform {
 public:
  virtual ~FakeLoudnessTransform() = default;

  float EvaluateStageGain(const Stage& stage) const override { return 0.; }
};

std::vector<std::shared_ptr<AudioObject>> SourcesOf(LinkMatrix* link_matrix, AudioObject* object) {
  std::vector<LinkMatrix::LinkHandle> handle_store;
  link_matrix->SourceLinks(*object, &handle_store);

  std::vector<std::shared_ptr<AudioObject>> store;
  std::transform(handle_store.begin(), handle_store.end(), std::back_inserter(store),
                 [](auto handle) { return handle.object; });
  return store;
}

std::vector<std::shared_ptr<AudioObject>> DestsOf(LinkMatrix* link_matrix, AudioObject* object) {
  std::vector<LinkMatrix::LinkHandle> handle_store;
  link_matrix->DestLinks(*object, &handle_store);

  std::vector<std::shared_ptr<AudioObject>> store;
  std::transform(handle_store.begin(), handle_store.end(), std::back_inserter(store),
                 [](auto handle) { return handle.object; });
  return store;
}

class LinkMatrixTest : public ::gtest::TestLoopFixture {
 protected:
  LinkMatrixTest() {}
};

TEST_F(LinkMatrixTest, EstablishesLinksSourcePerspective) {
  auto under_test = LinkMatrix();

  for (size_t i = 0; i < 10; ++i) {
    auto source = std::make_shared<MockObject>(AudioObject::Type::Input);
    auto dest = std::make_shared<MockObject>(AudioObject::Type::AudioCapturer);
    under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

    EXPECT_THAT(DestsOf(&under_test, source.get()), UnorderedElementsAreArray({dest}));
  }
}

TEST_F(LinkMatrixTest, EstablishesLinksDestPerspective) {
  auto under_test = LinkMatrix();

  for (size_t i = 0; i < 10; ++i) {
    auto source = std::make_shared<MockObject>(AudioObject::Type::Input);
    auto dest = std::make_shared<MockObject>(AudioObject::Type::AudioCapturer);
    under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

    EXPECT_THAT(SourcesOf(&under_test, dest.get()), UnorderedElementsAreArray({source}));
  }
}

TEST_F(LinkMatrixTest, RemovesLinksSourcePerspective) {
  auto under_test = LinkMatrix();

  for (size_t i = 0; i < 10; ++i) {
    auto source = std::make_shared<MockObject>(AudioObject::Type::Input);
    auto dest = std::make_shared<MockObject>(AudioObject::Type::AudioCapturer);

    under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

    under_test.Unlink(*source.get());

    EXPECT_THAT(SourcesOf(&under_test, dest.get()), IsEmpty());
    EXPECT_THAT(DestsOf(&under_test, source.get()), IsEmpty());
  }
}

TEST_F(LinkMatrixTest, RemovesLinksDestPerspective) {
  auto under_test = LinkMatrix();

  for (size_t i = 0; i < 10; ++i) {
    auto source = std::make_shared<MockObject>(AudioObject::Type::Input);
    auto dest = std::make_shared<MockObject>(AudioObject::Type::AudioCapturer);

    under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

    under_test.Unlink(*dest.get());

    EXPECT_THAT(DestsOf(&under_test, source.get()), IsEmpty());
    EXPECT_THAT(SourcesOf(&under_test, dest.get()), IsEmpty());
  }
}

TEST_F(LinkMatrixTest, DoesNotOwnObjects) {
  auto under_test = LinkMatrix();

  for (size_t i = 0; i < 10; ++i) {
    auto source = std::make_shared<MockObject>(AudioObject::Type::Output);
    auto dest = std::make_shared<MockObject>(AudioObject::Type::AudioCapturer);

    auto source_weak = std::weak_ptr<AudioObject>(source);
    auto dest_weak = std::weak_ptr<AudioObject>(dest);

    under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

    {
      auto source_discard = std::move(source);
      auto dest_discard = std::move(dest);
    }

    EXPECT_FALSE(source_weak.lock());
    EXPECT_FALSE(dest_weak.lock());
  }
}

std::string PrintType(const AudioObject::Type& to_print) {
  switch (to_print) {
    case AudioObject::Type::AudioRenderer:
      return "AudioRenderer";

    case AudioObject::Type::AudioCapturer:
      return "AudioCapturer";

    case AudioObject::Type::Output:
      return "Output";

    case AudioObject::Type::Input:
      return "Input";

    default:
      return "Invalid object type";
  };
}

TEST_F(LinkMatrixTest, ValidatesPairing) {
  for (auto& [pairing, valid] :
       std::vector<std::pair<std::pair<AudioObject::Type, AudioObject::Type>, bool>>{
           {{AudioObject::Type::AudioRenderer, AudioObject::Type::Output}, true},
           {{AudioObject::Type::AudioRenderer, AudioObject::Type::Input}, false},
           {{AudioObject::Type::AudioRenderer, AudioObject::Type::AudioRenderer}, false},
           {{AudioObject::Type::AudioRenderer, AudioObject::Type::AudioCapturer}, false},

           {{AudioObject::Type::AudioCapturer, AudioObject::Type::Output}, false},
           {{AudioObject::Type::AudioCapturer, AudioObject::Type::Input}, false},
           {{AudioObject::Type::AudioCapturer, AudioObject::Type::AudioRenderer}, false},
           {{AudioObject::Type::AudioCapturer, AudioObject::Type::AudioCapturer}, false},

           {{AudioObject::Type::Input, AudioObject::Type::Output}, false},
           {{AudioObject::Type::Input, AudioObject::Type::Input}, false},
           {{AudioObject::Type::Input, AudioObject::Type::AudioRenderer}, false},
           {{AudioObject::Type::Input, AudioObject::Type::AudioCapturer}, true},

           {{AudioObject::Type::Output, AudioObject::Type::Output}, false},
           {{AudioObject::Type::Output, AudioObject::Type::Input}, false},
           {{AudioObject::Type::Output, AudioObject::Type::AudioRenderer}, false},
           {{AudioObject::Type::Output, AudioObject::Type::AudioCapturer}, true}

       }) {
    auto [source_type, dest_type] = pairing;
    auto source = std::make_shared<MockObject>(source_type);
    auto dest = std::make_shared<MockObject>(dest_type);
    if (!valid) {
      ASSERT_DEATH(
          LinkMatrix().LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>()), "")
          << "Linking " << PrintType(source_type) << " with " << PrintType(dest_type);
    } else {
      LinkMatrix().LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());
    }
  }
}

const std::optional<LinkMatrix::LinkHandle> LinkFor(LinkMatrix* link_matrix, AudioObject* source,
                                                    AudioObject* dest) {
  std::vector<LinkMatrix::LinkHandle> handle_store;
  link_matrix->DestLinks(*source, &handle_store);

  auto handle = std::find_if(handle_store.begin(), handle_store.end(),
                             [dest](auto candidate) { return candidate.object.get() == dest; });
  if (handle == handle_store.end()) {
    return std::nullopt;
  }

  return {*handle};
}

const LoudnessTransform* TransformFor(LinkMatrix* link_matrix, AudioObject* source,
                                      AudioObject* dest) {
  auto handle = LinkFor(link_matrix, source, dest);
  if (!handle) {
    return nullptr;
  }

  return handle->loudness_transform.get();
}

TEST_F(LinkMatrixTest, LoudnessTransformIsAssociated) {
  auto under_test = LinkMatrix();

  auto source = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest1 = std::make_shared<MockObject>(AudioObject::Type::Output);
  auto dest2 = std::make_shared<MockObject>(AudioObject::Type::Output);

  auto tf1 = std::make_shared<FakeLoudnessTransform>();
  auto tf2 = std::make_shared<FakeLoudnessTransform>();

  under_test.LinkObjects(source, dest1, tf1);
  under_test.LinkObjects(source, dest2, tf2);

  EXPECT_EQ(TransformFor(&under_test, source.get(), dest1.get()), tf1.get());
  EXPECT_EQ(TransformFor(&under_test, source.get(), dest2.get()), tf2.get());
  EXPECT_EQ(TransformFor(&under_test, dest1.get(), source.get()), nullptr);
  EXPECT_EQ(TransformFor(&under_test, dest2.get(), source.get()), nullptr);
}

TEST_F(LinkMatrixTest, InitializationHooks) {
  auto under_test = LinkMatrix();

  auto source = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  auto stream = std::make_shared<PacketQueue>(
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value(),
      AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic()));
  source->set_stream(stream);

  under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

  EXPECT_EQ(source->dest(), dest.get());
  EXPECT_EQ(dest->source(), source.get());
  EXPECT_EQ(dest->stream(), stream);

  EXPECT_TRUE(source->on_link_added_called());
  EXPECT_TRUE(dest->on_link_added_called());
}

TEST_F(LinkMatrixTest, LinkHandleHasStream) {
  auto under_test = LinkMatrix();

  auto source = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  auto stream = std::make_shared<PacketQueue>(
      Format::Create({
                         .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                         .channels = 2,
                         .frames_per_second = 48000,
                     })
          .take_value(),
      AudioClock::CreateAsClientNonadjustable(clock::CloneOfMonotonic()));
  source->set_stream(stream);

  under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

  EXPECT_EQ(LinkFor(&under_test, source.get(), dest.get())->stream, stream);
}

TEST_F(LinkMatrixTest, LinkHandleHasMixer) {
  auto under_test = LinkMatrix();

  auto source = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  auto mixer = std::make_unique<audio::mixer::NoOp>();
  auto expected_mixer_addr = mixer.get();
  dest->set_mixer(std::move(mixer));

  under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());

  EXPECT_EQ(LinkFor(&under_test, source.get(), dest.get())->mixer.get(), expected_mixer_addr);
}

TEST_F(LinkMatrixTest, UnlinkCallsCleanupHooksSourcePerspective) {
  auto under_test = LinkMatrix();

  auto source = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());
  under_test.Unlink(*source.get());

  EXPECT_EQ(dest->cleaned_source_link(), source.get());
  EXPECT_EQ(source->cleaned_dest_link(), dest.get());
}

TEST_F(LinkMatrixTest, UnlinkCallsCleanupHooksDestPerspective) {
  auto under_test = LinkMatrix();

  auto source = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  under_test.LinkObjects(source, dest, std::make_shared<FakeLoudnessTransform>());
  under_test.Unlink(*dest.get());

  EXPECT_EQ(source->cleaned_dest_link(), dest.get());
  EXPECT_EQ(dest->cleaned_source_link(), source.get());
}

TEST_F(LinkMatrixTest, AreLinked) {
  auto under_test = LinkMatrix();

  auto source1 = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto source2 = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  under_test.LinkObjects(source1, dest, std::make_shared<FakeLoudnessTransform>());
  EXPECT_TRUE(under_test.AreLinked(*source1, *dest));
  EXPECT_FALSE(under_test.AreLinked(*dest, *source1));
  EXPECT_FALSE(under_test.AreLinked(*source2, *dest));

  under_test.Unlink(*dest);
  EXPECT_FALSE(under_test.AreLinked(*source1, *dest));
}

TEST_F(LinkMatrixTest, LinkCounts) {
  auto under_test = LinkMatrix();

  auto source1 = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto source2 = std::make_shared<MockObject>(AudioObject::Type::AudioRenderer);
  auto dest = std::make_shared<MockObject>(AudioObject::Type::Output);

  under_test.LinkObjects(source1, dest, std::make_shared<FakeLoudnessTransform>());
  under_test.LinkObjects(source2, dest, std::make_shared<FakeLoudnessTransform>());
  EXPECT_EQ(under_test.SourceLinkCount(*source1), 0u);
  EXPECT_EQ(under_test.SourceLinkCount(*dest), 2u);
  EXPECT_EQ(under_test.DestLinkCount(*source1), 1u);
  EXPECT_EQ(under_test.DestLinkCount(*source2), 1u);
}

}  // namespace
}  // namespace media::audio
