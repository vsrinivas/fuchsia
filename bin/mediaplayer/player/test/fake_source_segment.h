// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_SOURCE_SEGMENT_H_
#define GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_SOURCE_SEGMENT_H_

#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/player/source_segment.h"

namespace media_player {

// A source segment for testing the player.
class FakeSourceSegment : public SourceSegment {
 public:
  static std::unique_ptr<FakeSourceSegment> Create(
      fit::function<void(FakeSourceSegment*)> destroy_callback) {
    return std::make_unique<FakeSourceSegment>(std::move(destroy_callback));
  }

  FakeSourceSegment(fit::function<void(FakeSourceSegment*)> destroy_callback)
      : destroy_callback_(std::move(destroy_callback)) {
    FXL_DCHECK(destroy_callback_);
  }

  ~FakeSourceSegment() override { destroy_callback_(this); }

  // SourceSegment overrides.
  void DidProvision() override { did_provision_called_ = true; }

  void WillDeprovision() override { will_deprovision_called_ = true; }

  int64_t duration_ns() const override { return duration_ns_; };

  const Metadata* metadata() const override { return metadata_; }

  void Flush(bool hold_frame, fit::closure callback) override {
    flush_called_ = true;
    flush_call_param_hold_frame_ = hold_frame;
    callback();
  }

  void Seek(int64_t position, fit::closure callback) override {
    seek_called_ = true;
    seek_call_param_position_ = position;
    seek_call_param_callback_ = std::move(callback);
  }

  NodeRef source_node() const override { return NodeRef(); }

 public:
  // Protected calls exposed for testing.
  Graph& TEST_graph() { return graph(); }

  async_dispatcher_t* TEST_dispatcher() { return dispatcher(); }

  void TEST_NotifyUpdate() { NotifyUpdate(); }

  void TEST_ReportProblem(const std::string& type, const std::string& details) {
    ReportProblem(type, details);
  }

  void TEST_ReportNoProblem() { ReportNoProblem(); }

  bool TEST_provisioned() { return provisioned(); }

  void TEST_OnStreamUpdated(size_t index, const StreamType& type,
                            OutputRef output, bool more) {
    OnStreamUpdated(index, type, output, more);
  }

  void TEST_OnStreamRemoved(size_t index, bool more) {
    OnStreamRemoved(index, more);
  }

 public:
  // Instrumentation for test.
  fit::function<void(FakeSourceSegment*)> destroy_callback_;

  bool did_provision_called_ = false;
  bool will_deprovision_called_ = false;

  int64_t duration_ns_ = 0;
  const Metadata* metadata_ = nullptr;

  bool flush_called_ = false;
  bool flush_call_param_hold_frame_;

  bool seek_called_ = false;
  int64_t seek_call_param_position_;
  fit::closure seek_call_param_callback_;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_SOURCE_SEGMENT_H_
