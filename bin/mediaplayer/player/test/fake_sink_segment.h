// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_SINK_SEGMENT_H_
#define GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_SINK_SEGMENT_H_

#include <lib/fit/function.h>

#include "garnet/bin/mediaplayer/player/sink_segment.h"

namespace media_player {

// A sink segment for testing the player.
class FakeSinkSegment : public SinkSegment {
 public:
  static std::unique_ptr<FakeSinkSegment> Create(
      fit::function<void(FakeSinkSegment*)> destroy_callback) {
    return std::make_unique<FakeSinkSegment>(std::move(destroy_callback));
  }

  FakeSinkSegment(fit::function<void(FakeSinkSegment*)> destroy_callback)
      : destroy_callback_(std::move(destroy_callback)) {
    FXL_DCHECK(destroy_callback_);
  }

  ~FakeSinkSegment() override { destroy_callback_(this); }

  // SinkSegment overrides.
  void DidProvision() override { did_provision_called_ = true; }

  void WillDeprovision() override { will_deprovision_called_ = true; }

  void Connect(const StreamType& type, OutputRef output,
               ConnectCallback callback) override {
    connect_called_ = true;
    connect_call_param_type_ = &type;
    connect_call_param_output_ = output;
    connect_call_param_callback_ = std::move(callback);
  }

  void Disconnect() override { disconnect_called_ = true; }

  bool connected() const override { return connected_; }

  void Prepare() override { prepare_called_ = true; }

  void Unprepare() override { unprepare_called_ = true; }

  void Prime(fit::closure callback) override {
    prime_called_ = true;
    prime_call_param_callback_ = std::move(callback);
  }

  void SetTimelineFunction(media::TimelineFunction timeline_function,
                           fit::closure callback) override {
    set_timeline_function_called_ = true;
    set_timeline_function_call_param_timeline_function_ = timeline_function;
    set_timeline_function_call_param_callback_ = std::move(callback);
  }

  void SetProgramRange(uint64_t program, int64_t min_pts,
                       int64_t max_pts) override {
    set_program_range_called_ = true;
    set_program_range_call_param_program_ = program;
    set_program_range_call_param_min_pts_ = min_pts;
    set_program_range_call_param_max_pts_ = max_pts;
  }

  bool end_of_stream() const override { return end_of_stream_; }

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

 public:
  // Instrumentation for test.
  fit::function<void(FakeSinkSegment*)> destroy_callback_;

  bool did_provision_called_ = false;
  bool will_deprovision_called_ = false;

  bool connect_called_ = false;
  const StreamType* connect_call_param_type_;
  OutputRef connect_call_param_output_;
  ConnectCallback connect_call_param_callback_;

  bool disconnect_called_ = false;

  bool connected_ = false;

  bool prepare_called_ = false;

  bool unprepare_called_ = false;

  bool prime_called_ = false;
  fit::closure prime_call_param_callback_;

  bool set_timeline_function_called_ = false;
  media::TimelineFunction set_timeline_function_call_param_timeline_function_;
  fit::closure set_timeline_function_call_param_callback_;

  bool set_program_range_called_ = false;
  uint64_t set_program_range_call_param_program_;
  int64_t set_program_range_call_param_min_pts_;
  int64_t set_program_range_call_param_max_pts_;

  bool end_of_stream_ = false;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_PLAYER_TEST_FAKE_SINK_SEGMENT_H_
