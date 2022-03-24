// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_DIAGNOSTICS_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_DIAGNOSTICS_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include <atomic>
#include <functional>
#include <list>
#include <string_view>

#include <fbl/macros.h>

class DriverDiagnostics;

class CodecDiagnostics {
 public:
  struct TimePeriod {
    TimePeriod() = default;
    TimePeriod(zx::time in_end_time, zx::duration in_total_time, zx::duration in_active_time)
        : end_time(in_end_time), total_time(in_total_time), active_time(in_active_time) {}

    ~TimePeriod() = default;

    zx::time end_time;
    zx::duration total_time;
    zx::duration active_time;
  };

  explicit CodecDiagnostics(DriverDiagnostics& driver_diagnostics, inspect::Node root);
  ~CodecDiagnostics();

  // Nodes and NumericProperty should be cleaned up when this class
  // is destroyed, so ensure it is not copy to imply ownership so that
  // we can use a RAII approach.
  CodecDiagnostics(const CodecDiagnostics&) = delete;
  CodecDiagnostics& operator=(const CodecDiagnostics&) = delete;
  CodecDiagnostics(CodecDiagnostics&&) = default;
  CodecDiagnostics& operator=(CodecDiagnostics&&) = default;

  void UpdateHardwareUtilizationStatus(zx::time now, bool is_utilizing_hardware);

 private:
  void PostToSharedDiagnostics(fit::closure operation);

  static constexpr std::string_view kCurrentlyUtilizingHardware = "utilizing_hardware";
  static constexpr std::string_view kAllocation = "allocation_pct";
  static constexpr std::string_view kTotalAllocatedTime = "total_allocated_time_ns";

  static constexpr zx::duration kMemoryDuration = zx::msec(100);
  static constexpr zx::duration kBucketDuration = zx::msec(50);

  static std::atomic<trace_counter_id_t> CurrentTraceCounter;

  // Store a reference to the drive diagnostic that created us, however
  // we can not outlive it.
  // Note: This has to be in a reference wrapper inorder to support the move
  // assignment operator.
  std::reference_wrapper<DriverDiagnostics> driver_diagnostics_;
  std::list<TimePeriod> time_periods_;
  inspect::Node root_;
  inspect::UintProperty utilizing_hardware_;
  inspect::DoubleProperty allocation_;
  inspect::UintProperty total_allocated_time_;
  zx::time last_checked_time_ = zx::clock::get_monotonic();
  bool currently_utilizing_hardware_ = false;
  trace_counter_id_t trace_counter_id_ = CurrentTraceCounter++;
};

class DriverDiagnostics {
 public:
  explicit DriverDiagnostics(std::string_view driver_name);
  ~DriverDiagnostics();

  // Nodes and NumericProperty should be cleaned up when this class
  // is destroyed, so ensure it is not copy to imply ownership so that
  // we can use a RAII approach.
  DriverDiagnostics(const DriverDiagnostics&) = delete;
  DriverDiagnostics& operator=(const DriverDiagnostics&) = delete;
  DriverDiagnostics(DriverDiagnostics&&) = delete;
  DriverDiagnostics& operator=(DriverDiagnostics&&) = delete;

  zx::vmo DuplicateVmo() const;

  // Sets the the time of the driver binding to the device at the current value
  // of the monotic clock.
  void SetBindTime();

  void IncrementCurrentlyDecoding();
  void DecrementCurrentlyDecoding();

  // Creates a codec diagnostic instance with the given name. The name is guaranteed
  // to be unique.
  CodecDiagnostics CreateCodec(std::string_view codec_name);

  // Should be called when a codec is unloaded.
  void RemoveCodec();

  void PostToSharedDiagnostics(fit::closure operation);

 private:
  static constexpr std::string_view kBindTime = "bind_time";
  static constexpr std::string_view kNumOfActiveCodecs = "num_of_active_codecs";
  static constexpr std::string_view kCurrentlyDecoding = "currently_decoding";

  inspect::Inspector inspector_;
  inspect::Node root_;
  inspect::UintProperty bind_time_;
  inspect::UintProperty num_of_active_codecs_;
  inspect::BoolProperty currently_decoding_;
  int32_t num_of_currently_decoding_ = 0;

  // TODO(fxbug.dev/96016)
  // This is a workaround for the tracing API. Currently the only tracing macro that supports
  // changing the scope to global, process or thread is TRACE_INSTANT meaning that if the driver
  // uses different threads for tracing calls it will show up as two different tracks in the
  // perfetto user interface. To workaround this issue create a diagnostics thread and post tasks
  // to the shared_diagnostics_loop_ dispatcher instead of calling it on the current thread.
  async::Loop shared_diagnostics_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  thrd_t shared_diagnostics_thread_;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_DIAGNOSTICS_H_
