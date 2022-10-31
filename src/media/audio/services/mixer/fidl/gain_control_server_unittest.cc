// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/gain_control_server.h"

#include <fidl/fuchsia.audio/cpp/wire_types.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <lib/fidl/cpp/wire/object_view.h>
#include <lib/fidl/cpp/wire/wire_types.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "fidl/fuchsia.audio/cpp/common_types.h"
#include "fidl/fuchsia.audio/cpp/natural_types.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/common/testing/test_server_and_sync_client.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/common/global_task_queue.h"
#include "src/media/audio/services/mixer/fidl/graph_detached_thread.h"
#include "src/media/audio/services/mixer/fidl/mixer_node.h"
#include "src/media/audio/services/mixer/fidl/ptr_decls.h"
#include "src/media/audio/services/mixer/fidl/testing/fake_graph.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::GainError;

fidl::WireTableBuilder<fuchsia_audio::wire::GainControlSetGainRequest> MakeDefaultSetGainRequest(
    fidl::AnyArena& arena) {
  return fuchsia_audio::wire::GainControlSetGainRequest::Builder(arena)
      .when(fuchsia_audio::wire::GainTimestamp::WithImmediately({}))
      .how(fuchsia_audio::wire::GainUpdateMethod::WithGainDb(6.0f));
}

fidl::WireTableBuilder<fuchsia_audio::wire::GainControlSetMuteRequest> MakeDefaultSetMuteRequest(
    fidl::AnyArena& arena) {
  return fuchsia_audio::wire::GainControlSetMuteRequest::Builder(arena)
      .when(fuchsia_audio::wire::GainTimestamp::WithImmediately({}))
      .muted(true);
}

class GainControlServerTest : public ::testing::Test {
 public:
  void SetUp() {
    global_task_queue_ = std::make_shared<GlobalTaskQueue>();
    thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
    wrapper_ = std::make_unique<TestServerAndWireSyncClient<GainControlServer>>(
        thread_, GainControlServer::Args{
                     .id = GainControlId{1},
                     .reference_clock = DefaultClock(),
                     .global_task_queue = global_task_queue_,
                 });
  }

  void TearDown() {
    // Close the client and wait until the server shuts down.
    wrapper_.reset();
  }

  NodePtr MakeDefaultMixer() {
    return MixerNode::Create(MixerNode::Args{
        .pipeline_direction = PipelineDirection::kOutput,
        .format = Format::CreateOrDie({
            .sample_type = fuchsia_audio::SampleType::kFloat32,
            .channels = 1,
            .frames_per_second = 48000,
        }),
        .dest_buffer_frame_count = 1,
        .detached_thread = std::make_shared<GraphDetachedThread>(global_task_queue_),
    });
  }

  GainControlServer& server() { return wrapper_->server(); }
  fidl::WireSyncClient<fuchsia_audio::GainControl>& client() { return wrapper_->client(); }

 protected:
  fidl::Arena<> arena_;

 private:
  std::shared_ptr<GlobalTaskQueue> global_task_queue_;
  std::shared_ptr<FidlThread> thread_;
  std::unique_ptr<TestServerAndWireSyncClient<GainControlServer>> wrapper_;
};

TEST_F(GainControlServerTest, SetGainFails) {
  struct TestCase {
    std::string name;
    std::function<void(fidl::WireTableBuilder<fuchsia_audio::wire::GainControlSetGainRequest>&)>
        edit;
    GainError expected_error;
  };
  std::vector<TestCase> cases = {
      {
          .name = "MissingWhen",
          .edit = [](auto& request) { request.clear_when(); },
          .expected_error = GainError::kMissingRequiredField,
      },
      {
          .name = "MissingHow",
          .edit = [](auto& request) { request.clear_how(); },
          .expected_error = GainError::kMissingRequiredField,
      },
  };

  for (auto& tc : cases) {
    SCOPED_TRACE("TestCase: " + tc.name);
    auto request = MakeDefaultSetGainRequest(arena_);
    tc.edit(request);

    auto result = client()->SetGain(request.Build());

    if (!result.ok()) {
      ADD_FAILURE() << "failed to send method call: " << result;
      continue;
    }
    if (!result->is_error()) {
      ADD_FAILURE() << "SetGain did not fail";
      continue;
    }
    EXPECT_EQ(result->error_value(), tc.expected_error);

    EXPECT_FLOAT_EQ(server().gain_control().state().gain_db, kUnityGainDb);
  }
}

TEST_F(GainControlServerTest, AddRemoveMixer) {
  EXPECT_EQ(server().num_mixers(), 0ul);

  // Add a mixer.
  const auto mixer_1 = MakeDefaultMixer();
  server().AddMixer(mixer_1);
  EXPECT_EQ(server().num_mixers(), 1ul);

  // Remove the mixer.
  server().RemoveMixer(mixer_1);
  EXPECT_EQ(server().num_mixers(), 0ul);
}

TEST_F(GainControlServerTest, SetGainSuccess) {
  auto result = client()->SetGain(MakeDefaultSetGainRequest(arena_).Build());
  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result->is_error());

  EXPECT_FLOAT_EQ(server().gain_control().state().gain_db, 6.0f);
}

TEST_F(GainControlServerTest, SetMuteFails) {
  struct TestCase {
    std::string name;
    std::function<void(fidl::WireTableBuilder<fuchsia_audio::wire::GainControlSetMuteRequest>&)>
        edit;
    GainError expected_error;
  };
  std::vector<TestCase> cases = {
      {
          .name = "MissingWhen",
          .edit = [](auto& request) { request.clear_when(); },
          .expected_error = GainError::kMissingRequiredField,
      },
      {
          .name = "MissingMuted",
          .edit = [](auto& request) { request.clear_muted(); },
          .expected_error = GainError::kMissingRequiredField,
      },
  };

  for (auto& tc : cases) {
    SCOPED_TRACE("TestCase: " + tc.name);
    auto request = MakeDefaultSetMuteRequest(arena_);
    tc.edit(request);

    auto result = client()->SetMute(request.Build());

    if (!result.ok()) {
      ADD_FAILURE() << "failed to send method call: " << result;
      continue;
    }
    if (!result->is_error()) {
      ADD_FAILURE() << "SetMute did not fail";
      continue;
    }
    EXPECT_EQ(result->error_value(), tc.expected_error);

    EXPECT_EQ(server().gain_control().state().is_muted, false);
  }
}

TEST_F(GainControlServerTest, SetMuteSuccess) {
  auto result = client()->SetMute(MakeDefaultSetMuteRequest(arena_).Build());
  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result->is_error());

  EXPECT_EQ(server().gain_control().state().is_muted, true);
}

}  // namespace
}  // namespace media_audio
