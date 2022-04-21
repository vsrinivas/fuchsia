// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_METRICS_REPORTER_H_
#define SRC_CAMERA_BIN_DEVICE_METRICS_REPORTER_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/sys/inspect/cpp/component.h>

#include <mutex>

#include "src/camera/lib/cobalt_logger/logger.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

namespace camera {

// |MetricsReporter| handles instrumentation concerns (e.g. exposing information via inspect,
// cobalt, etc) for a camera device instance.
class MetricsReporter {
 public:
  ~MetricsReporter() = default;

  // Forward class declarations
  class ConfigurationRecord;
  class ImageFormatRecord;
  class StreamRecord;
  class FailureTestRecord;

  // Gets a reference to the MetricsReporter object.
  //
  // Returns a reference of a static MetricsReporter object.  If this is called before
  // MetricsReporter::Initialize(), a nop version of MetricsReporter is returned and swallows all
  // log requests.
  static MetricsReporter& Get();

  // Initializes the MetricsReporter object.
  //
  // |context| References to the services and connections this component was launched with.
  //
  // |enable_cobalt| When this boolean argument is true, MetricsReporter initializes the
  //     CobaltLogger and report collected metrics.
  static void Initialize(sys::ComponentContext& context, bool enable_cobalt)
      FXL_LOCKS_EXCLUDED(mutex_);

  // Get a reference to the inspector object.
  const inspect::Inspector& inspector() FXL_LOCKS_EXCLUDED(mutex_) {
    std::lock_guard<std::mutex> lock(mutex_);
    return *impl_->inspector_->inspector();
  }

  // Creates a ConfigurationRecord object that collects metrics from each device configurations.
  //
  // |index| The index of the device configuration that will be associated with this
  // ConfigurationRecord object.
  //
  // |num_streams| This is the number of streams that the configuration with a given index has.
  std::unique_ptr<ConfigurationRecord> CreateConfigurationRecord(uint32_t index,
                                                                 size_t num_streams);

  using FailureTestRecordType = camera__metrics::CameraMetricDimensionFailureTestType;
  // Creates a FailureTestRecord representing a potential failure occurrence. When the class is
  // destroyed, the occurrence is logged as either failure or non-failure depending on its state at
  // the time of destruction. Callers can use the |initial_failure| parameter and ignore the
  // returned record to provide immediate-mode recording.
  //
  // |type| The failure class for the record.
  //
  // |initial_failure| The initial failure state of the record.
  //
  // |config_index| If specified, the configuration this failure applies to.
  //
  // |stream_index| If specified, the stream this failure applies to.
  std::unique_ptr<FailureTestRecord> CreateFailureTestRecord(
      FailureTestRecordType type, bool initial_failure = true,
      std::optional<uint32_t> config_index = std::nullopt,
      std::optional<uint32_t> stream_index = std::nullopt);

 private:
  explicit MetricsReporter() = default;
  explicit MetricsReporter(sys::ComponentContext& context, bool enable_cobalt);

  struct Impl {
    sys::ComponentContext& context_;
    std::unique_ptr<sys::ComponentInspector> inspector_;

    inspect::Node node_;

    std::unique_ptr<cobalt::Logger> logger_;

    explicit Impl(sys::ComponentContext& context);
    ~Impl() = default;
  };

  void InitInspector() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void ConnectToLogger(::fidl::InterfaceRequest<fuchsia::metrics::MetricEventLogger> request);
  void RetryConnectingToLogger();

  mutable std::mutex mutex_;
  std::unique_ptr<Impl> impl_ FXL_GUARDED_BY(mutex_);

 public:
  // An object of the ImageFormatRecord class stores the output format of an associated stream.
  class ImageFormatRecord {
   public:
    explicit ImageFormatRecord(inspect::Node& parent);

    // Stores the image format property.
    //
    // |format| Reference to ImageFormat_2 object.
    void Set(const fuchsia::sysmem::ImageFormat_2& format);

   private:
    inspect::Node node_;
    inspect::StringProperty pixel_format_;
    inspect::StringProperty output_resolution_;
    inspect::StringProperty display_resolution_;
    inspect::StringProperty color_space_;
    inspect::StringProperty pixel_aspect_ratio_;
  };

  // An object of the StreamRecord class monitors each stream, collects metrics, and reports them
  // via the Cobalt logger if it is enabled.  Some of collected metrics are available via the
  // Inspect API.
  class StreamRecord {
   public:
    StreamRecord(MetricsReporter::Impl& impl, inspect::Node& parent, uint32_t config_index,
                 uint32_t stream_index);

    // Stores the properties of current stream.
    //
    // |props| References to StreamProperties2 object that contains the size information, the
    //     output format, and the frame rate.
    void SetProperties(const fuchsia::camera3::StreamProperties2& props);

    // Reports that we've received a new frame.
    void FrameReceived();

    // Reports that we've dropped/skipped a newly delivered frame.
    void FrameDropped(cobalt::FrameDropReason why);

   private:
    MetricsReporter::Impl& impl_;

    zx::time stream_start_;
    zx::time stream_stop_;

    inspect::Node node_;
    inspect::StringProperty frame_rate_;
    inspect::BoolProperty supports_crop_region_;
    inspect::Node supported_resolutions_node_;
    std::vector<inspect::StringProperty> supported_resolutions_;

    ImageFormatRecord format_record_;

    inspect::UintProperty frames_received_;
    inspect::UintProperty frames_dropped_;

    cobalt::StreamType type_;
  };

  // An object of the ConfigurationRecord class monitors the device configuration associated with
  // a given configuration index and collects metrics to report via the Cobalt logger or share via
  // the Inspect API.
  class ConfigurationRecord {
   public:
    ConfigurationRecord(MetricsReporter::Impl& impl, uint32_t index, size_t num_streams);
    virtual ~ConfigurationRecord() = default;

    // Gets a reference to a StreamRecord object associated with a given stream index.
    //
    // |index| The index of the stream the caller is interested in.
    StreamRecord& GetStreamRecord(uint32_t index) { return stream_records_[index]; }

    // Marks the configuration is active or not.
    //
    // |active| This boolean value tells whether this configuration is active or not.
    void SetActive(bool active) { active_node_.Set(active); }

   private:
    inspect::Node node_;
    inspect::BoolProperty active_node_;
    inspect::Node stream_node_;
    std::vector<StreamRecord> stream_records_;
  };

  // FailureTestRecord records a potential occurrence of a specific class of failure in the camera
  // system, and subsequently whether or not the failure actually occurred.
  class FailureTestRecord {
   public:
    explicit FailureTestRecord(MetricsReporter::Impl& impl, FailureTestRecordType type,
                               bool initial_failure, std::optional<uint32_t> config_index,
                               std::optional<uint32_t> stream_index);
    ~FailureTestRecord();

    // Sets the record failure state to the provided value.
    void SetFailureState(bool failed);

   private:
    MetricsReporter::Impl& impl_;
    FailureTestRecordType type_;
    bool failed_;
    camera__metrics::CameraMetricDimensionConfigIndex config_index_;
    camera__metrics::CameraMetricDimensionStreamIndex stream_index_;
  };
};

}  // namespace camera

inline constexpr const char kConfigurationInspectorActivePropertyName[] = "active";
inline constexpr const char kConfigurationInspectorNodeName[] = "configurations";
inline constexpr const char kFormatInspectorAspectRatioPropertyName[] = "aspect ratio";
inline constexpr const char kFormatInspectorColorSpacePropertyName[] = "color space";
inline constexpr const char kFormatInspectorDisplayResolutionPropertyName[] = "display resolution";
inline constexpr const char kFormatInspectorOutputResolutionPropertyName[] = "output resolution";
inline constexpr const char kFormatInspectorPixelformatPropertyName[] = "pixel format";
inline constexpr const char kStreamInspectorCropPropertyName[] = "supports crop region";
inline constexpr const char kStreamInspectorFrameratePropertyName[] = "frame rate";
inline constexpr const char kStreamInspectorFramesDroppedPropertyName[] = "frames dropped";
inline constexpr const char kStreamInspectorFramesReceivedPropertyName[] = "frames received";
inline constexpr const char kStreamInspectorImageFormatNodeName[] = "image format";
inline constexpr const char kStreamInspectorNodeName[] = "streams";
inline constexpr const char kStreamInspectorResolutionNodeName[] = "supported resolutions";

#endif  // SRC_CAMERA_BIN_DEVICE_METRICS_REPORTER_H_
