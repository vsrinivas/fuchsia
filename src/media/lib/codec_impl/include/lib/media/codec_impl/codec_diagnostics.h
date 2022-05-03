// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_DIAGNOSTICS_H_
#define SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_DIAGNOSTICS_H_

#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/inspector.h>
#include <lib/inspect/cpp/vmo/types.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include <atomic>
#include <list>
#include <memory>
#include <string_view>

#include <fbl/macros.h>
#include <trace-vthread/event_vthread.h>

class CodecDiagnostics;

// Class is used to store codec inspect data for a codec where we do not have control of the
// underlying hardware or the codec is software based. Since we do not control the hardware, or
// there is no underlying hardware acceleration, we cannot report the utilization of the codec.
class ComponentCodecDiagnostics {
 public:
  explicit ComponentCodecDiagnostics(CodecDiagnostics& driver_diagnostics, inspect::Node root);
  ~ComponentCodecDiagnostics();

  // Nodes and NumericProperty should be cleaned up when this class
  // is destroyed, so ensure it is not copy to imply ownership so that
  // we can use a RAII approach.
  ComponentCodecDiagnostics(const ComponentCodecDiagnostics&) = delete;
  ComponentCodecDiagnostics& operator=(const ComponentCodecDiagnostics&) = delete;
  ComponentCodecDiagnostics(ComponentCodecDiagnostics&&) = default;
  ComponentCodecDiagnostics& operator=(ComponentCodecDiagnostics&&) = default;

 private:
  static constexpr std::string_view kCreationTime = "creation_time";

  std::reference_wrapper<CodecDiagnostics> driver_diagnostics_;
  inspect::Node root_;
  inspect::UintProperty creation_time_;
};

// Class is used to store codec inspect data for a codec where we control the underlying
// hardware (example: amlogic driver). Because we control the hardware we can calculate
// utilization of the underlying hardware and report it via the inspect data.
class DriverCodecDiagnostics {
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

  explicit DriverCodecDiagnostics(CodecDiagnostics& driver_diagnostics, inspect::Node root);
  ~DriverCodecDiagnostics();

  // Nodes and NumericProperty should be cleaned up when this class
  // is destroyed, so ensure it is not copy to imply ownership so that
  // we can use a RAII approach.
  DriverCodecDiagnostics(const DriverCodecDiagnostics&) = delete;
  DriverCodecDiagnostics& operator=(const DriverCodecDiagnostics&) = delete;
  DriverCodecDiagnostics(DriverCodecDiagnostics&&) = default;
  DriverCodecDiagnostics& operator=(DriverCodecDiagnostics&&) = default;

  void UpdateHardwareUtilizationStatus(zx::time now, bool is_utilizing_hardware);

 private:
  static constexpr std::string_view kCreationTime = "creation_time";
  static constexpr std::string_view kCurrentlyUtilizingHardware = "utilizing_hardware";
  static constexpr std::string_view kAllocation = "allocation_pct";
  static constexpr std::string_view kTotalAllocatedTime = "total_allocated_time_ns";

  static constexpr zx::duration kMemoryDuration = zx::msec(1000);
  static constexpr zx::duration kBucketDuration = zx::msec(50);

  static std::atomic<trace_counter_id_t> CurrentTraceCounter;

  // Store a reference to the drive diagnostic that created us, however
  // we can not outlive it.
  // Note: This has to be in a reference wrapper inorder to support the move
  // assignment operator.
  std::reference_wrapper<CodecDiagnostics> driver_diagnostics_;
  std::list<TimePeriod> time_periods_;
  inspect::Node root_;
  inspect::UintProperty creation_time_;
  inspect::UintProperty utilizing_hardware_;
  inspect::DoubleProperty allocation_;
  inspect::UintProperty total_allocated_time_;
  zx::time last_checked_time_ = zx::clock::get_monotonic();
  bool currently_utilizing_hardware_ = false;
  trace_counter_id_t trace_counter_id_ = CurrentTraceCounter++;
};

class CodecDiagnostics {
 public:
  explicit CodecDiagnostics(std::string_view driver_name);

  // For non-driver components we need to accept the context since the inspector has to be wired to
  // the component's output directory
  CodecDiagnostics(std::unique_ptr<sys::ComponentContext>& context, std::string_view driver_name);

  // Nodes and NumericProperty should be cleaned up when this class
  // is destroyed, so ensure it is not copy to imply ownership so that
  // we can use a RAII approach.
  CodecDiagnostics(const CodecDiagnostics&) = delete;
  CodecDiagnostics& operator=(const CodecDiagnostics&) = delete;
  CodecDiagnostics(CodecDiagnostics&&) = delete;
  CodecDiagnostics& operator=(CodecDiagnostics&&) = delete;

  zx::vmo DuplicateVmo() const;

  // Sets the the time of the driver binding to the device at the current value
  // of the monotic clock.
  void SetBindTime();

  void IncrementCurrentlyDecoding();
  void DecrementCurrentlyDecoding();

  // Creates a driver codec diagnostic instance with the given name. The name is guaranteed to be
  // unique.
  DriverCodecDiagnostics CreateDriverCodec(std::string_view codec_name);

  // Create a component codec diagnostic instance with the given name.
  ComponentCodecDiagnostics CreateComponentCodec(std::string_view codec_name);

  // Should be called when a codec is unloaded.
  void RemoveCodec();

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
};

// Wrapper class that allows for the getting and setting of a decoder state. When setting
// the decoder state the class will update trace data to reflect the current decoder state and
// also calls the UpdateDiagnostics() on the VideoDecoder class to update the decoder's diagnostics.
template <typename StateType>
class DiagnosticStateWrapper {
 public:
  // State is an enum so get the underlying type for casting
  using UnderlyingType = std::underlying_type_t<StateType>;
  DiagnosticStateWrapper(fit::closure update_diagnostics_function, StateType state_value,
                         fit::function<const char*(StateType)> state_name_function)
      : update_diagnostics_function_(std::move(update_diagnostics_function)),
        state_value_(state_value),
        state_name_function_(std::move(state_name_function)),
        vthread_id_(GetNextVthreadID()) {
    TRACE_VTHREAD_DURATION_BEGIN("media", state_name_function_(state_value_), "Decoder",
                                 vthread_id_, zx_ticks_get());
  }

  ~DiagnosticStateWrapper() {
    TRACE_VTHREAD_DURATION_END("media", state_name_function_(state_value_), "Decoder", vthread_id_,
                               zx_ticks_get());
  }

  // Wrapper assignment operator. When a different state is assigned, end the current trace for
  // this decoder and start a trace for the new state, update the underlying state and call
  // UpdateDiagnostics() so the decoder's diagnostics are updated
  DiagnosticStateWrapper& operator=(StateType new_statue) {
    // Only process updates if the state has changed
    if (state_value_ != new_statue) {
      TRACE_VTHREAD_DURATION_END("media", state_name_function_(state_value_), "Decoder",
                                 vthread_id_, zx_ticks_get());
      state_value_ = new_statue;
      TRACE_VTHREAD_DURATION_BEGIN("media", state_name_function_(state_value_), "Decoder",
                                   vthread_id_, zx_ticks_get());
      update_diagnostics_function_();
    }

    return *this;
  }

  // Comparison operators, just passthrough to the underlying state
  bool operator==(StateType other_state) const noexcept { return (state_value_ == other_state); }
  bool operator!=(StateType other_state) const noexcept { return (state_value_ != other_state); }

  explicit operator StateType() const noexcept { return state_value_; }

  explicit operator UnderlyingType() const noexcept {
    return static_cast<UnderlyingType>(state_value_);
  }

 private:
  static trace_vthread_id_t GetNextVthreadID() {
    static std::atomic<uint64_t> id;
    // Vthread IDs are rounded to the nearest 1000 due to the double->float conversion. See
    // fxbug.dev/22971/
    constexpr uint32_t kVthreadIdDistance = 2000;
    return id.fetch_add(kVthreadIdDistance);
  }

  fit::closure update_diagnostics_function_;
  StateType state_value_;
  fit::function<const char*(StateType)> state_name_function_;
  const trace_vthread_id_t vthread_id_;
};

#endif  // SRC_MEDIA_LIB_CODEC_IMPL_INCLUDE_LIB_MEDIA_CODEC_IMPL_CODEC_DIAGNOSTICS_H_
