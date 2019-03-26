// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_H_
#define GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_H_

#include <stdint.h>

#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace dockyard {

// An integer value representing a sample stream name.
typedef uint32_t SampleStreamId;
// Sample time stamp in nanoseconds.
typedef uint64_t SampleTimeNs;
// The data type of a sample value.
typedef uint64_t SampleValue;
// This is not intended to remain a std::map. This works fine for small numbers
// of samples and it has the API desired. So a std::map is being used while
// framing out the API.
typedef std::map<SampleTimeNs, SampleValue> SampleStream;

// This is clearer than using the raw number.
constexpr SampleTimeNs kNanosecondsPerSecond = 1000000000;

// Special value for missing sample stream.
constexpr SampleValue NO_STREAM = (SampleValue)-1ULL;
// Special value for missing data.
constexpr SampleValue NO_DATA = (SampleValue)-2ULL;
// The highest value for sample data.
constexpr SampleValue SAMPLE_MAX_VALUE = (SampleValue)-3ULL;

// The slope value is scaled up to preserve decimal precision when using an
// integer value. To convert the slope integer (slope_value) to floating point:
// float slope_as_percentage = float(slope_value) * SLOPE_SCALE.
constexpr SampleValue SLOPE_LIMIT = 1000000ULL;
constexpr float SLOPE_SCALE = 100.0f / float(SLOPE_LIMIT);

// The upper value used to represent zero to one values with integers.
constexpr SampleValue NORMALIZATION_RANGE = 1000000ULL;

// For compatibility check with the Harvester.
constexpr uint32_t DOCKYARD_VERSION = 1;

enum KoidType : SampleValue {
  JOB = 100ULL,
  PROCESS = 101ULL,
  THREAD = 102ULL,
};

// A Sample.
struct Sample {
  Sample(SampleTimeNs t, SampleValue v) : time(t), value(v) {}

  SampleTimeNs time;
  // Sample values range from [0 to SAMPLE_MAX_VALUE].
  SampleValue value;
};

// Mapping between IDs and name strings.
struct StreamInfo {
  // The ID that corresponds to |name|, below.
  SampleStreamId id;
  // The name that corresponds to |id|, above.
  std::string name;
};

struct StreamSetsRequest {
  enum RenderStyle {
    // When smoothing across samples, use a wider set of samples, including
    // samples that are just outside of the sample set range. E.g. if the range
    // is time 9 to 18, smooth over time 7 to 20.
    WIDE_SMOOTHING,
    // When sculpting across samples, pull the result toward the peaks and
    // valleys in the data (rather than showing the average).
    SCULPTING,
    // For each column of the output, use the least value from the samples.
    LOWEST_PER_COLUMN,
    // For each column of the output, use the greatest value from the samples.
    HIGHEST_PER_COLUMN,
    // Add up the sample values for the slice of time and divide by the number
    // of values found (i.e. take the average or mean).
    AVERAGE_PER_COLUMN,
  };

  enum StreamSetsRequestFlags {
    // Frame (or scale) the data set aesthetically. E.g. if the graph has little
    // variance, zoom in to show that detail, rather then just having a flat
    // vertical line in the graph. In some cases (like comparing graphs) this
    // will be undesired. The values in the response will be in the range
    // [0 to NORMALIZATION_RANGE].
    NORMALIZE = 1 << 0,
    // Compute the slope of the curve.
    SLOPE = 1 << 1,
  };

  StreamSetsRequest()
      : request_id(0),
        start_time_ns(0),
        end_time_ns(0),
        sample_count(0),
        min(0),
        max(0),
        reserved(0),
        render_style(AVERAGE_PER_COLUMN),
        flags(0) {}

  // For matching against a StreamSetsResponse::request_id.
  uint64_t request_id;

  // Request graph data for time range |start_time..end_time| that has
  // |sample_count| values for each set. If the sample stream has more or less
  // samples for that time range, virtual samples will be generated based on
  // available samples.
  SampleTimeNs start_time_ns;
  SampleTimeNs end_time_ns;
  uint64_t sample_count;

  SampleValue min;    // Future use.
  SampleValue max;    // Future use.
  uint64_t reserved;  // Future use.

  RenderStyle render_style;
  uint64_t flags;

  // A StreamInfo::id for each stream of interest.
  std::vector<SampleStreamId> stream_ids;

  bool HasFlag(StreamSetsRequestFlags flag) const;

  friend std::ostream& operator<<(std::ostream& os,
                                  const StreamSetsRequest& request);
};

struct StreamSetsResponse {
  // For matching against a StreamSetsRequest::request_id.
  uint64_t request_id;

  // The low and high all-time values for all sample streams requested. All-time
  // means that these low and high points might not appear in the |data_sets|
  // below. "All sample streams" means that these points may not appear in the
  // same sample streams.
  SampleValue lowest_value;
  SampleValue highest_value;

  // Each data set will correspond to a stream requested in the
  // StreamSetsRequest::stream_ids. The value for each sample is normally in
  // the range [0 to SAMPLE_MAX_VALUE]. If no value exists for the column, the
  // value NO_DATA is used. If the StreamSetsRequest::stream_id was not found,
  // the resulting sample will have the value NO_STREAM.
  std::vector<std::vector<SampleValue>> data_sets;

  friend std::ostream& operator<<(std::ostream& os,
                                  const StreamSetsResponse& response);
};

// Lookup for a sample stream name string, given the sample stream ID.
typedef std::map<SampleStreamId, std::string> StreamIdNameMap;
typedef std::map<std::string, SampleStreamId> StreamNameIdMap;

// Called when new streams are added or removed. Added values include their ID
// and string name. Removed values only have the ID.
// Intended to inform clients of StreamInfoMap changes (so they may keep their
// equivalent map in sync). The racy nature of this update is not an issue
// because the rest of the API will cope with invalid stream IDs, so 'eventually
// consistent' is acceptable).
// Use SetStreamNamesHandler() to install a StreamNamesCallback callback.
typedef std::function<void(const std::vector<StreamInfo>& add,
                           const std::vector<SampleStreamId>& remove)>
    StreamNamesCallback;

// Called after (and in response to) a request is sent to |GetStreamSets()|.
// Use SetStreamSetsHandler() to install a StreamSetsCallback callback.
typedef std::function<void(const StreamSetsResponse& response)>
    StreamSetsCallback;

class Dockyard {
 public:
  Dockyard();
  ~Dockyard();

  // Insert sample information for a given stream_id. Not intended for use by
  // the GUI.
  void AddSample(SampleStreamId stream_id, Sample sample);

  // Insert sample information for a given stream_id. Not intended for use by
  // the GUI.
  void AddSamples(SampleStreamId stream_id, std::vector<Sample> samples);

  // The *approximate* difference between host time and device time. This value
  // is negotiated at connection time and not reevaluated. If either clock is
  // altered this value may be wildly inaccurate. The intended use of this value
  // is to hint the GUI when displaying sample times (not for doing CI analysis
  // or similar computations).
  // If the value is positive then the device clock is ahead of the host clock.
  // Given a sample, subtract this value to get the host time.
  // Given a host time, add this value to get device (sample) time.
  // See: LatestSampleTimeNs()
  SampleTimeNs DeviceDeltaTimeNs() const;

  // Helper functions to compute time. Read important details in the description
  // of |DeviceDeltaTimeNs|.
  SampleTimeNs DeviceTimeToHostTime(SampleTimeNs device_time_ns) const {
    return device_time_ns - device_time_delta_ns_;
  }
  SampleTimeNs HostTimeToDeviceTime(SampleTimeNs host_time_ns) const {
    return host_time_ns + device_time_delta_ns_;
  };

  void SetDeviceTimeDeltaNs(SampleTimeNs delta_ns);

  // The time stamp for the most recent batch of samples to arrive. The time is
  // device time (not host time) in nanoseconds.
  // See: DeviceDeltaTimeNs()
  SampleTimeNs LatestSampleTimeNs() const;

  // Get sample stream identifier for a given name. The ID values are stable
  // throughout execution, so they may be cached.
  //
  // Returns an ID that corresponds to |name|.
  SampleStreamId GetSampleStreamId(const std::string& name);

  // Request graph data for time range |start_time..end_time| that has
  // |sample_count| values for each set. If the sample stream has more or less
  // samples for that time range, virtual samples will be generated based on
  // available samples.
  //
  // The results will be supplied in a call to the |callback| previously set
  // with SetStreamSetsHandler(). The |response| parameter on that callback will
  // have the same context ID that is returned from this call to
  // GetStreamSets() (i.e. that's how to match a response to a request).
  //
  // Returns unique context ID.
  uint64_t GetStreamSets(StreamSetsRequest* request);

  // Start collecting data from a named device. Tip: device names are normally
  // four short words, such as "duck floor quick rock".
  void StartCollectingFrom(const std::string& device);
  void StopCollectingFrom(const std::string& device);

  // Sets the function called when sample streams are added or removed. Pass
  // nullptr as |callback| to stop receiving calls.
  //
  // Returns prior callback or nullptr.
  StreamNamesCallback SetStreamNamesHandler(StreamNamesCallback callback);

  // Sets the function called when sample stream data arrives in response to a
  // call to GetStreamSets(). So, first set a handler with
  // SetStreamSetsHandler(), then make as many GetStreamSets() calls as
  // desired. Pass nullptr as |callback| to stop receiving calls.
  //
  // Returns prior callback or nullptr.
  StreamSetsCallback SetStreamSetsHandler(StreamSetsCallback callback);

  // Generate responses and call handlers for sample requests. Not intended for
  // use by the GUI.
  void ProcessRequests();

 private:
  // TODO(dschuyler): avoid having a global mutex. Use a queue to update data.
  mutable std::mutex mutex_;
  std::thread server_thread_;

  // The time (clock) on the device will likely differ from the host.
  SampleTimeNs device_time_delta_ns_;
  SampleTimeNs latest_sample_time_ns_;

  // Communication with the GUI.
  StreamNamesCallback stream_name_handler_;
  StreamSetsCallback stream_sets_handler_;
  std::vector<StreamSetsRequest*> pending_requests_;

  // Storage of sample data.
  typedef std::map<SampleStreamId, SampleStream*> SampleStreamMap;
  SampleStreamMap sample_streams_;
  std::map<SampleStreamId, std::pair<SampleValue, SampleValue>>
      sample_stream_low_high_;

  // Name <--> ID look up.
  uint64_t next_context_id_;
  StreamNameIdMap stream_ids_;
  StreamIdNameMap stream_names_;

  // Listen for incoming samples.
  bool Initialize();

  // Each of these Compute*() methods aggregate samples in different ways.
  // There's no single 'true' way to represent aggregated data, so the choice
  // is left to the caller. Which of these is used depends on the
  // |StreamSetsRequestFlags| in the |StreamSetsRequest.flags| field.
  void ComputeAveragePerColumn(SampleStreamId stream_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const;
  void ComputeHighestPerColumn(SampleStreamId stream_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const;
  void ComputeLowestPerColumn(SampleStreamId stream_id,
                              const SampleStream& sample_stream,
                              const StreamSetsRequest& request,
                              std::vector<SampleValue>* samples) const;
  void ComputeSculpted(SampleStreamId stream_id,
                       const SampleStream& sample_stream,
                       const StreamSetsRequest& request,
                       std::vector<SampleValue>* samples) const;
  void ComputeSmoothed(SampleStreamId stream_id,
                       const SampleStream& sample_stream,
                       const StreamSetsRequest& request,
                       std::vector<SampleValue>* samples) const;

  // Rework the response so that all values are in the range 0 to one million.
  // This represents a 0.0 to 1.0 value, scaled up.
  void NormalizeResponse(SampleStreamId stream_id,
                         const SampleStream& sample_stream,
                         const StreamSetsRequest& request,
                         std::vector<SampleValue>* samples) const;

  void ComputeLowestHighestForRequest(const StreamSetsRequest& request,
                                      StreamSetsResponse* response) const;

  // The average of the lowest and highest value in the stream.
  SampleValue OverallAverageForStream(SampleStreamId stream_id) const;

  // Gather the overall lowest and highest values encountered.
  void ProcessSingleRequest(const StreamSetsRequest& request,
                            StreamSetsResponse* response) const;
};

}  // namespace dockyard

#endif  // GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_H_
