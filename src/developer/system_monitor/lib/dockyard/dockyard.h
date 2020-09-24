// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_LIB_DOCKYARD_DOCKYARD_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_LIB_DOCKYARD_DOCKYARD_H_

#include <stdint.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class SystemMonitorDockyardHostTest;
namespace grpc {
class Server;
}  // namespace grpc

namespace dockyard {

class DockyardServiceImpl;
class SystemMonitorDockyardTest;

// The default address to use to reach the Dockyard gRPC server.
extern const char kDefaultServerAddress[];

// An integer value representing a dockyard path.
typedef uint32_t DockyardId;
constexpr DockyardId INVALID_DOCKYARD_ID = 0;
// Sample time stamp in nanoseconds.
typedef uint64_t SampleTimeNs;
// The data type of a sample value.
typedef uint64_t SampleValue;
// This is not intended to remain a std::map. This works fine for small numbers
// of samples and it has the API desired. So a std::map is being used while
// framing out the API.
using SampleStream = std::map<SampleTimeNs, SampleValue>;

// This is clearer than using the raw number.
constexpr SampleTimeNs kNanosecondsPerSecond = 1000000000;
constexpr SampleTimeNs kSampleTimeInfinite = UINT64_MAX;

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
constexpr float SLOPE_SCALE = 100.0f / static_cast<float>(SLOPE_LIMIT);

// The upper value used to represent zero to one values with integers.
constexpr SampleValue NORMALIZATION_RANGE = 1000000ULL;

// For compatibility check with the Harvester.
constexpr uint32_t DOCKYARD_VERSION = 2;

enum KoidType : SampleValue {
  JOB = 100ULL,
  PROCESS = 101ULL,
  THREAD = 102ULL,
  CHANNEL = 103ULL,
};

// A Sample.
struct Sample {
  Sample(SampleTimeNs t, SampleValue v) : time(t), value(v) {}

  SampleTimeNs time;
  // Sample values range from [0 to SAMPLE_MAX_VALUE].
  SampleValue value;
};

// Mapping between IDs and path strings.
struct PathInfo {
  // The dockyard ID that corresponds to |path|, below.
  DockyardId id;
  // The dockyard path that corresponds to |id|, above.
  std::string path;
};

// Avoid removing elements from this enum.
enum class MessageType : int64_t {
  // A response to a k*Request message. Match request IDs to determine which
  // request this is a response to.
  kResponseOk = 0,
  // The request (represented by the request ID) failed in some fundamental way.
  // E.e. maybe the request never made it to the handler.
  kRequestFailed = -1,
  // The connection to the Harvester on Fuchsia device has broken. No further
  // requests will work until a new connection is established.
  kDisconnected = -2,
  // The version of the Harvester is incompatible with the Dockyard.
  kVersionMismatch = -3,

  // Requests from the UI to the Dockyard.
  kStreamSetsRequest = 1,
  kDiscardSamplesRequest,
  kIgnoreSamplesRequest,
  kUnignoreSamplesRequest,
  kConnectionRequest,
  kSampleStreamsRequest,
};

std::ostream& operator<<(std::ostream& os, MessageType message_type);

// A message to or from the dockyard.
struct Message {
  // The request ID normally matches a request to a response. In the case of a
  // 'push' message with no request, the ID will be |NULL_REQUEST_ID|.
  static constexpr uint64_t NULL_REQUEST_ID = {0u};

  // Context identifier for a message. Used to match a response to a request.
  // For matching against a MessageResponse::request_id.
  Message(MessageType type, uint64_t id)
      : message_type_(type), request_id_(id) {}

  // The message type helps in routing the message. Expect to use both the
  // message type and the request ID together (all current cases require both
  // values).
  MessageType GetMessageType() const { return message_type_; }

  // Context identifier for a message. Used to match a response to a request.
  uint64_t RequestId() const { return request_id_; }

 protected:
  // Identifier used for message routing, i.e. which code should handle this
  // message.
  MessageType message_type_;

  // Context identifier for a message. Used to match a response to a request.
  uint64_t request_id_;
};

// A message to the dockyard. The response to this message will arrive as a
// |MessageResponse| with a matching |RequestId()|.
struct MessageRequest : public Message {
  MessageRequest(MessageType type) : Message(type, ++next_request_id_) {}

 private:
  // There is no rollover (wrap around) guard for the ID value. It's expected
  // that a 64 bit integer is large enough to eliminate concern about it.
  static uint64_t next_request_id_;
};

// A message from the dockyard.
struct MessageResponse : public Message {
  MessageResponse()
      : Message(MessageType::kResponseOk, Message::NULL_REQUEST_ID) {}

  void SetMessageType(MessageType message_type) {
    message_type_ = message_type;
  }

  bool Ok() const { return message_type_ == MessageType::kResponseOk; }

  // The request ID defaults to a 'push' message with a null request ID. If this
  // is a response to a specific request, set the request ID to the request's
  // request ID.
  void SetRequestId(uint64_t id) { request_id_ = id; }
};

// Ask that the Dockyard make a connection to a Harvester running on a Fuchsia
// device.
struct ConnectionRequest : public MessageRequest {
  ConnectionRequest() : MessageRequest(MessageType::kConnectionRequest) {}

  const std::string& DeviceName() const { return device_name_; }
  void SetDeviceName(std::string name) { device_name_ = name; }

 private:
  std::string device_name_;
};

// A |ConnectionResponse| is a reply for an individual
// |ConnectionRequest|.
// See: ConnectionRequest.
struct ConnectionResponse : public MessageResponse {
  ConnectionResponse(uint32_t dockyard_version, uint32_t harvester_version)
      : dockyard_version_(dockyard_version),
        harvester_version_(harvester_version) {}

  uint32_t DockyardVersion() const { return dockyard_version_; }
  uint32_t HarvesterVersion() const { return harvester_version_; }

 private:
  uint32_t dockyard_version_;
  uint32_t harvester_version_;
};

// To delete/remove samples from a sample stream, create a DiscardSamplesRequest
// for the desired time range (by default it will remove all samples for the
// stream) and pass the struct to Dockyard::DiscardSamples().
struct DiscardSamplesRequest : public MessageRequest {
  DiscardSamplesRequest()
      : MessageRequest(MessageType::kDiscardSamplesRequest) {}

  // Request that samples are for time range |start_time..end_time|. Defaults to
  // all samples (time zero to kSampleTimeInfinite). If there is no positive
  // difference between start and end, the request will not have an effect.
  SampleTimeNs start_time_ns = 0;
  SampleTimeNs end_time_ns = kSampleTimeInfinite;

  // Each stream is identified by a Dockyard ID. Multiple streams can be
  // discarded.
  std::vector<DockyardId> dockyard_ids;

  friend std::ostream& operator<<(std::ostream& out,
                                  const DiscardSamplesRequest& request);
};

// A |DiscardSamplesResponse| is a reply for an individual
// |DiscardSamplesRequest|.
// See: DiscardSamplesRequest.
struct DiscardSamplesResponse : MessageResponse {
  DiscardSamplesResponse() = default;

  friend std::ostream& operator<<(std::ostream& out,
                                  const DiscardSamplesResponse& response);
};

// To ignore samples, i.e. prevent them from being tracked, create a
// IgnoreSamplesRequest that will match the beginning and ending of the stream
// paths to ignore.
struct IgnoreSamplesRequest : public MessageRequest {
  IgnoreSamplesRequest() : MessageRequest(MessageType::kIgnoreSamplesRequest) {}

  std::string prefix;
  std::string suffix;
};

// An |IgnoreSamplesResponse| is a reply for an individual
// |IgnoreSamplesRequest|.
// See: IgnoreSamplesRequest.
struct IgnoreSamplesResponse : public MessageResponse {
  IgnoreSamplesResponse() = default;
};

// A stream set is a portion of a sample stream. This request allows for
// requesting multiple stream sets in a single request. The results will arrive
// in the form of a |StreamSetsResponse|.
//
// Note: Set an |OnStreamSetsCallback| with |SetStreamSetsHandler()|
//       before using the request to be sure of getting the message that the
//       request is complete.
//
// See: StreamSetsResponse.
struct StreamSetsRequest : public MessageRequest {
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
    // Get the single, most recent value prior to |end_time_ns|. Generally used
    // with |start_time_ns| of zero, but |start_time_ns| can still be used to
    // restrict the time range.
    // The |flags| NORMALIZE and SLOPE are ignored when using RECENT.
    RECENT,
  };

  enum StreamSetsRequestFlags {
    // Frame (or scale) the data set aesthetically. E.g. if the graph has little
    // variance, zoom in to show that detail, rather than just having a flat
    // vertical line in the graph. In some cases (like comparing graphs) this
    // will be undesired. The values in the response will be in the range
    // [0 to NORMALIZATION_RANGE].
    NORMALIZE = 1 << 0,
    // Compute the slope of the curve.
    SLOPE = 1 << 1,
  };

  StreamSetsRequest()
      : MessageRequest(MessageType::kStreamSetsRequest),
        start_time_ns(0),
        end_time_ns(0),
        sample_count(0),
        min(0),
        max(0),
        reserved(0),
        render_style(AVERAGE_PER_COLUMN),
        flags(0) {}

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

  // Each stream is identified by a Dockyard ID. Multiple streams can be
  // requested. Include a DockyardId for each stream of interest.
  std::vector<DockyardId> dockyard_ids;

  bool HasFlag(StreamSetsRequestFlags flag) const;

  friend std::ostream& operator<<(std::ostream& out,
                                  const StreamSetsRequest& request);
};

// A |StreamSetsResponse| is a reply for an individual |StreamSetsRequest|.
// See: StreamSetsRequest.
struct StreamSetsResponse : MessageResponse {
  StreamSetsResponse() = default;

  // The low and high all-time values for all sample streams requested. All-time
  // means that these low and high points might not appear in the |data_sets|
  // below. "All sample streams" means that these points may not appear in the
  // same sample streams.
  SampleValue lowest_value;
  SampleValue highest_value;

  // Each data set will correspond to a stream requested in the
  // StreamSetsRequest::dockyard_ids. The value for each sample is normally in
  // the range [0 to SAMPLE_MAX_VALUE]. If no value exists for the column, the
  // value NO_DATA is used.
  // For any DockyardId from StreamSetsRequest::dockyard_ids that isn't found,
  // the resulting samples will have the value NO_STREAM.
  std::vector<std::vector<SampleValue>> data_sets;

  friend std::ostream& operator<<(std::ostream& out,
                                  const StreamSetsResponse& response);
};

// This request allows for requesting multiple sample streams in a single
// request. The results will arrive in the form of a |SampleStreamsResponse|.
//
// Note: Set an |OnSampleStreamsCallback| with |SampleStreamsHandler()|
//       before using the request to be sure of getting the message that the
//       request is complete.
//
// See: SampleStreamsResponse.
struct SampleStreamsRequest : public MessageRequest {
  SampleStreamsRequest()
      : MessageRequest(MessageType::kSampleStreamsRequest),
        start_time_ns(0),
        end_time_ns(0) {}

  SampleTimeNs start_time_ns;
  SampleTimeNs end_time_ns;

  // Each stream is identified by a Dockyard ID. Multiple streams can be
  // requested. Include a DockyardId for each stream of interest.
  std::vector<DockyardId> dockyard_ids;

  friend std::ostream& operator<<(std::ostream& out,
                                  const SampleStreamsRequest& request);
};

// A |SampleStreamsResponse| is a reply for an individual
// |SampleStreamsRequest|. See: SampleStreamsRequest.
struct SampleStreamsResponse : MessageResponse {
  SampleStreamsResponse() = default;

  // The low and high all-time values for all sample streams requested. All-time
  // means that these low and high points might not appear in the |data_sets|
  // below. "All sample streams" means that these points may not appear in the
  // same sample streams.
  SampleValue lowest_value;
  SampleValue highest_value;

  // Each data set will correspond to a stream requested in the
  // SampleStreamsRequest::dockyard_ids.
  std::vector<std::vector<std::pair<SampleTimeNs, SampleValue>>> data_sets;

  friend std::ostream& operator<<(std::ostream& out,
                                  const SampleStreamsResponse& response);
};

// To stop ignoring samples, create a  UnignoreSamplesRequest that will match
// the |prefix| and |suffix| values from a prior IgnoreSamplesRequest.
struct UnignoreSamplesRequest : public MessageRequest {
  UnignoreSamplesRequest()
      : MessageRequest(MessageType::kUnignoreSamplesRequest) {}

  std::string prefix;
  std::string suffix;
};

// An |UnignoreSamplesResponse| is a reply for an individual
// |UnignoreSamplesRequest|.
// See: UnignoreSamplesRequest.
struct UnignoreSamplesResponse : MessageResponse {
  UnignoreSamplesResponse() = default;
};

class SampleStreamMap
    : public std::map<DockyardId, std::unique_ptr<SampleStream>> {
 public:
  // Get a reference to the sample stream for the given |dockyard_id|.
  // The stream will be created if necessary.
  SampleStream& StreamRef(DockyardId dockyard_id) {
    return *emplace(dockyard_id, std::make_unique<SampleStream>())
                .first->second;
  }
};

// Lookup for a sample stream name string, given the sample stream ID.
using DockyardIdToPathMap = std::map<DockyardId, std::string>;
using DockyardPathToIdMap = std::map<std::string, DockyardId>;

// Called when a request to ignore samples is complete.
using IgnoreSamplesCallback =
    std::function<void(const IgnoreSamplesRequest& request,
                       const IgnoreSamplesResponse& response)>;

// Called when a connection is made between the Dockyard and Harvester on a
// Fuchsia device.
using OnConnectionCallback = std::function<void(
    const ConnectionRequest& request, const ConnectionResponse& response)>;

// Called when new streams are added or removed. Added values include their ID
// and string path. Removed values only have the ID.
// Intended to inform clients of PathInfoMap changes (so they may keep
// their equivalent map in sync). The racy nature of this update is not an issue
// because the rest of the API will cope with invalid stream IDs, so 'eventually
// consistent' is acceptable).
// Use SetDockyardPathsHandler() to install a StreamCallback callback.
using OnPathsCallback = std::function<void(
    const std::vector<PathInfo>& add, const std::vector<DockyardId>& remove)>;

// Called after (and in response to) a request is sent to |GetStreamSets()|.
// Use SetStreamSetsHandler() to install a StreamSetsCallback callback.
using OnStreamSetsCallback = std::function<void(
    const StreamSetsRequest& request, const StreamSetsResponse& response)>;

// Called after (and in response to) a request is sent to |GetSampleStreams()|.
// Use SetSampleStreamsHandler() to install a SampleStreamsCallback callback.
using OnSampleStreamsCallback =
    std::function<void(const SampleStreamsRequest& request,
                       const SampleStreamsResponse& response)>;

// Called after (and in response to) a request is sent to |DiscardSamples()|.
using OnDiscardSamplesCallback =
    std::function<void(const DiscardSamplesRequest& request,
                       const DiscardSamplesResponse& response)>;

class Dockyard {
 public:
  Dockyard();
  ~Dockyard();

  // Insert sample information for a given dockyard_id. Not intended for use by
  // the GUI.
  void AddSample(DockyardId dockyard_id, Sample sample);

  // Insert sample information for a given dockyard_id. Not intended for use by
  // the GUI.
  void AddSamples(DockyardId dockyard_id, const std::vector<Sample>& samples);

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
  }

  // Discard the stream data. The path/ID lookup will remain intact after the
  // discard (i.e. MatchPaths() will still find the paths).
  void DiscardSamples(DiscardSamplesRequest&& request,
                      OnDiscardSamplesCallback callback);

  // Set the difference in clocks between the host machine and the Fuchsia
  // device, in nanoseconds.
  void SetDeviceTimeDeltaNs(SampleTimeNs delta_ns);

  // The time stamp for the most recent batch of samples to arrive. The time is
  // device time (not host time) in nanoseconds.
  // See: DeviceDeltaTimeNs()
  SampleTimeNs LatestSampleTimeNs() const;

  // Get Dockyard identifier for a given path. The ID values are stable
  // throughout execution, so they may be cached.
  //
  // Returns a Dockyard ID that corresponds to |dockyard_path|.
  DockyardId GetDockyardId(const std::string& dockyard_path);

  // Determine whether |dockyard_path| is valid (if it exists).
  bool HasDockyardPath(const std::string& dockyard_path,
                       DockyardId* dockyard_id) const;

  // Translate a |dockyard_id| to a |dockyard_path|. |dockyard_path| is an out
  // value. Returns false if |dockyard_id| is unknown.
  bool GetDockyardPath(DockyardId dockyard_id,
                       std::string* dockyard_path) const;

  // Search the existing paths for those that start with |starting| and end with
  // |ending|. This is similar to having a single '*' wildcard search, something
  // akin to find all "$starting*$ending".
  // Returns a map of paths to IDs that is a subset of (or equal to) all known
  // stream sets.
  DockyardPathToIdMap MatchPaths(const std::string& starting,
                                 const std::string& ending) const;

  // Request graph data for time range |start_time..end_time| that has
  // |sample_count| values for each set. If the sample stream has more or less
  // samples for that time range, virtual samples will be generated based on
  // available samples.
  //
  // The results will be supplied in a call to the |callback|.
  void GetStreamSets(StreamSetsRequest&& request,
                     OnStreamSetsCallback callback);

  // Request sample stream data for time range |start_time..end_time|.
  //
  // The results will be supplied in a call to the |callback|.
  void GetSampleStreams(SampleStreamsRequest&& request,
                        OnSampleStreamsCallback callback);

  // Ignore subsequent samples per |request|. Note that existing (prior) samples
  // are not removed/discarded. To remove samples see: DiscardSamples().
  void IgnoreSamples(IgnoreSamplesRequest&& request,
                     IgnoreSamplesCallback callback);

  // Called by server when a connection is made.
  void OnConnection(MessageType message_type, uint32_t harvester_version);

  // Start collecting data from a named device. Tip: device names are normally
  // four short words, such as "duck-floor-quick-rock". If |StartCollectingFrom|
  // was previously called, call |StopCollectingFromDevice| before starting a
  // new connection (otherwise this call will fail and return false).
  // |server_address| is the address the dockyard should use for
  // its gRPC server.
  //
  // Returns true if successful.
  bool StartCollectingFrom(ConnectionRequest&& request,
                           OnConnectionCallback callback,
                           std::string server_adress = kDefaultServerAddress);

  // The inverse of |StartCollectingFrom|. It's safe to call this regardless of
  // whether |StartCollectingFrom| succeeded (no work is done unless
  // |StartCollectingFrom| had succeeded).
  void StopCollectingFromDevice();

  // Sets the function called when sample streams are added or removed. Pass
  // nullptr as |callback| to stop receiving calls.
  //
  // Returns prior callback or nullptr.
  OnPathsCallback SetDockyardPathsHandler(OnPathsCallback callback);

  // Generate responses and call handlers for sample requests. Not intended for
  // use by the GUI.
  void ProcessRequests();

  // Clear out the samples and other data that has been collected by the
  // harvester. This is not normally used unless the host wishes to reset the
  // data when a new connection is made.
  void ResetHarvesterData();

  // Write a snapshot of the current dockyard state to a string. Note that this
  // could be rather large. As the name implies it's intended for debugging
  // only.
  std::ostringstream DebugDump() const;

 private:
  // TODO(fxbug.dev/38): avoid having a global mutex. Use a queue to update
  // data.
  mutable std::mutex mutex_;
  std::thread server_thread_;

  // The server handles grpc messages (runs in a background thread).
  std::unique_ptr<grpc::Server> grpc_server_;

  // The port bound to the gRPC service.
  int grpc_server_port_;

  // The service handles proto buffers. The |service_| must remain valid until
  // the |server_| (which holds a weak pointer to |service_|) is finished.
  std::unique_ptr<DockyardServiceImpl> protocol_buffer_service_;

  // The time (clock) on the device will likely differ from the host.
  SampleTimeNs device_time_delta_ns_;
  SampleTimeNs latest_sample_time_ns_;

  // Communication with the GUI.
  ConnectionRequest on_connection_request_;
  OnConnectionCallback on_connection_handler_;
  OnPathsCallback on_paths_handler_;

  std::vector<std::pair<DiscardSamplesRequest, OnDiscardSamplesCallback>>
      pending_discard_requests_owned_;
  std::vector<std::pair<SampleStreamsRequest, OnSampleStreamsCallback>>
      pending_raw_get_requests_owned_;
  std::vector<std::pair<StreamSetsRequest, OnStreamSetsCallback>>
      pending_get_requests_owned_;
  std::vector<std::pair<IgnoreSamplesRequest, IgnoreSamplesCallback>>
      pending_ignore_samples_owned_;

  // Storage of sample data.
  SampleStreamMap sample_streams_;
  std::map<DockyardId, std::pair<SampleValue, SampleValue>>
      sample_stream_low_high_;

  // Track the ignore requests that have been made so that future paths that
  // match the ignore list can be added to the ignore_dockyard_ids_.
  std::set<std::pair<std::string, std::string>> ignore_streams_;
  // Ignore list derived from |ignore_streams_|. This is an optimization over
  // checking each update against the ignore_streams_. Doing a set lookup is
  // much less expensive.
  std::set<DockyardId> ignore_dockyard_ids_;

  // Dockyard path <--> ID look up.
  DockyardPathToIdMap dockyard_path_to_id_;
  DockyardIdToPathMap dockyard_id_to_path_;

  // Each of these Compute*() methods aggregate samples in different ways.
  // There's no single 'true' way to represent aggregated data, so the choice
  // is left to the caller. Which of these is used depends on the
  // |StreamSetsRequestFlags| in the |StreamSetsRequest.flags| field.
  void ComputeAveragePerColumn(DockyardId dockyard_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const;
  void ComputeHighestPerColumn(DockyardId dockyard_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const;
  void ComputeLowestPerColumn(DockyardId dockyard_id,
                              const SampleStream& sample_stream,
                              const StreamSetsRequest& request,
                              std::vector<SampleValue>* samples) const;
  void ComputeRecent(DockyardId dockyard_id, const SampleStream& sample_stream,
                     const StreamSetsRequest& request,
                     std::vector<SampleValue>* samples) const;
  void ComputeSculpted(DockyardId dockyard_id,
                       const SampleStream& sample_stream,
                       const StreamSetsRequest& request,
                       std::vector<SampleValue>* samples) const;
  void ComputeSmoothed(DockyardId dockyard_id,
                       const SampleStream& sample_stream,
                       const StreamSetsRequest& request,
                       std::vector<SampleValue>* samples) const;

  void ComputeLowestHighestForRequest(const StreamSetsRequest& request,
                                      StreamSetsResponse* response) const;

  void ComputeLowestHighestForSampleStreamsRequest(
      const SampleStreamsRequest& request,
      SampleStreamsResponse* response) const;

  // A private version of GetDockyardId that expects that a |mutex_| lock has
  // already been acquired.
  DockyardId GetDockyardIdLocked(const std::string& dockyard_path);

  // Ignore all further stream set data received that matches the patterns.
  // Existing samples are not removed by this call. This call expects that a
  // lock on |mutex_| has already been acquired.
  void IgnoreSamplesLocked(const std::string& starting,
                           const std::string& ending);

  // Listen for incoming samples.
  //
  // Returns |false| on problems with starting the gRPC server.
  bool Initialize(std::string server_adress);

  // A private version of MatchPaths that expects that a lock has already been
  // acquired.
  DockyardPathToIdMap MatchPathsLocked(const std::string& starting,
                                       const std::string& ending) const;

  // Rework the response so that all values are in the range 0 to one million.
  // This represents a 0.0 to 1.0 value, scaled up.
  void NormalizeResponse(DockyardId dockyard_id,
                         const SampleStream& sample_stream,
                         const StreamSetsRequest& request,
                         std::vector<SampleValue>* samples) const;

  // The average of the lowest and highest value in the stream.
  SampleValue OverallAverageForStream(DockyardId dockyard_id) const;

  // Processes the requests entered by DiscardSamples().
  void ProcessDiscardSamples(const DiscardSamplesRequest& request,
                             DiscardSamplesResponse* response);

  // Process a request to ignore samples.
  void ProcessIgnoreSamples(const IgnoreSamplesRequest& request,
                            IgnoreSamplesResponse* response);

  // Gather the overall lowest and highest values encountered.
  void ProcessSingleRequest(const StreamSetsRequest& request,
                            StreamSetsResponse* response) const;

  // Process a single request for sample streams.
  void ProcessSingleSampleStreamsRequest(const SampleStreamsRequest& request,
                                         SampleStreamsResponse* response) const;

  // Listen for Harvester connections from the Fuchsia device.
  void RunGrpcServer();

  friend class ::SystemMonitorDockyardHostTest;
  friend class ::dockyard::SystemMonitorDockyardTest;
  friend std::ostream& operator<<(std::ostream& out, const Dockyard& dockyard);
};

// Merge and print a request and response. It can make debugging easier to have
// the data correlated.
std::ostringstream DebugPrintQuery(const Dockyard& dockyard,
                                   const StreamSetsRequest& request,
                                   const StreamSetsResponse& response);

}  // namespace dockyard

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_LIB_DOCKYARD_DOCKYARD_H_
