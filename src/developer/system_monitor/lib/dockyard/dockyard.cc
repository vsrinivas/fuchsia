// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "src/developer/system_monitor/lib/dockyard/dockyard_service_impl.h"
#include "src/developer/system_monitor/lib/gt_log.h"

namespace dockyard {

namespace {

// Determine whether |haystack| begins with |needle|.
inline bool StringBeginsWith(const std::string& haystack,
                             const std::string& needle) {
  return std::mismatch(needle.begin(), needle.end(), haystack.begin()).first ==
         needle.end();
}

// Determine whether |haystack| ends with |needle|.
inline bool StringEndsWith(const std::string& haystack,
                           const std::string& needle) {
  return std::mismatch(needle.rbegin(), needle.rend(), haystack.rbegin())
             .first == needle.rend();
}

// To calculate the slope, a range of time is needed. |prior_time| and  |time|
// define that range. The very first |prior_time| is one stride prior to the
// requested start time.
SampleValue calculate_slope(SampleValue value, SampleValue* prior_value,
                            SampleTimeNs time, SampleTimeNs* prior_time) {
  if (*prior_value == 0) {
    // The sampling/smoothing functions below will use a prior_value of 0 if
    // there is no actual prior value. In this case, there's no valid slope
    // value, so update the prior time/value and return NO_DATA.
    *prior_value = value;
    *prior_time = time;
    return NO_DATA;
  } else if (value < *prior_value) {
    // A lower value will produce a negative slope, which is not currently
    // supported. As a workaround the value is pulled up to |prior_value| to
    // create a convex surface.
    value = *prior_value;
  }
  assert(time >= *prior_time);
  SampleValue delta_value = value - *prior_value;
  SampleTimeNs delta_time = time - *prior_time;
  SampleValue result =
      delta_time ? delta_value * SLOPE_LIMIT / delta_time : 0ULL;
  *prior_value = value;
  *prior_time = time;
  return result;
}

// Calculates the (edge) time for each column of the result data.
SampleTimeNs CalcTimeForStride(const StreamSetsRequest& request,
                               ssize_t index) {
  // These need to be signed to support a signed |index|.
  int64_t delta = (request.end_time_ns - request.start_time_ns);
  int64_t count = int64_t(request.sample_count);
  return request.start_time_ns + (delta * index / count);
}

}  // namespace

// This is an arbitrary default port.
const char kDefaultServerAddress[] = "0.0.0.0:50051";

std::ostream& operator<<(std::ostream& os, MessageType message_type) {
  switch (message_type) {
    case MessageType::kResponseOk:
      os << "ResponseOk";
      break;
    case MessageType::kRequestFailed:
      os << "RequestFailed";
      break;
    case MessageType::kDisconnected:
      os << "Disconnected";
      break;
    case MessageType::kVersionMismatch:
      os << "VersionMismatch";
      break;
    case MessageType::kStreamSetsRequest:
      os << "StreamSetsRequest";
      break;
    case MessageType::kDiscardSamplesRequest:
      os << "DiscardSamplesRequest";
      break;
    case MessageType::kIgnoreSamplesRequest:
      os << "IgnoreSamplesRequest";
      break;
    case MessageType::kUnignoreSamplesRequest:
      os << "UnignoreSamplesRequest";
      break;
    case MessageType::kConnectionRequest:
      os << "ConnectionRequest";
      break;
    default:
      os << "<Unknown MessageType>";
  }
  return os;
}

uint64_t MessageRequest::next_request_id_;

bool StreamSetsRequest::HasFlag(StreamSetsRequestFlags flag) const {
  return (flags & flag) != 0;
}

std::ostream& operator<<(std::ostream& out,
                         const DiscardSamplesRequest& request) {
  out << "DiscardSamplesRequest {" << std::endl;
  out << "  RequestId: " << request.RequestId() << std::endl;
  out << "  start_time_ns: " << request.start_time_ns << std::endl;
  out << "  end_time_ns:   " << request.end_time_ns << std::endl;
  out << "    delta time in seconds: "
      << double(request.end_time_ns - request.start_time_ns) /
             kNanosecondsPerSecond
      << std::endl;
  out << "  ids (" << request.dockyard_ids.size() << "): [";
  for (const auto& dockyard_id : request.dockyard_ids) {
    out << " " << dockyard_id;
  }
  out << " ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const DiscardSamplesResponse& response) {
  out << "DiscardSamplesResponse {" << std::endl;
  out << "  RequestId: " << response.RequestId() << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out, const StreamSetsRequest& request) {
  out << "StreamSetsRequest {" << std::endl;
  out << "  RequestId: " << request.RequestId() << std::endl;
  out << "  start_time_ns: " << request.start_time_ns << std::endl;
  out << "  end_time_ns:   " << request.end_time_ns << std::endl;
  out << "    delta time in seconds: "
      << double(request.end_time_ns - request.start_time_ns) /
             kNanosecondsPerSecond
      << std::endl;
  out << "  sample_count: " << request.sample_count << std::endl;
  out << "  min: " << request.min;
  out << "  max: " << request.max;
  out << "  reserved: " << request.reserved << std::endl;
  out << "  render_style: " << request.render_style;
  out << "  flags: " << request.flags << std::endl;
  out << "  ids (" << request.dockyard_ids.size() << "): [";
  for (const auto& dockyard_id : request.dockyard_ids) {
    out << " " << dockyard_id;
  }
  out << " ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const StreamSetsResponse& response) {
  out << "StreamSetsResponse {" << std::endl;
  out << "  RequestId: " << response.RequestId() << std::endl;
  out << "  lowest_value: " << response.lowest_value << std::endl;
  out << "  highest_value: " << response.highest_value << std::endl;
  out << "  data_sets (" << response.data_sets.size() << "): [" << std::endl;
  for (const auto& list : response.data_sets) {
    out << "    data_set: {";
    for (const auto& data : list) {
      if (data == NO_DATA) {
        out << " NO_DATA";
      } else {
        out << " " << data;
      }
    }
    out << " }, " << std::endl;
  }
  out << "  ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const SampleStreamsRequest& request) {
  out << "SampleStreamsRequest {" << std::endl;
  out << "  RequestId: " << request.RequestId() << std::endl;
  out << "  start_time_ns: " << request.start_time_ns << std::endl;
  out << "  end_time_ns:   " << request.end_time_ns << std::endl;
  out << "    delta time in seconds: "
      << double(request.end_time_ns - request.start_time_ns) /
             kNanosecondsPerSecond
      << std::endl;
  out << "  ids (" << request.dockyard_ids.size() << "): [";
  for (const auto& dockyard_id : request.dockyard_ids) {
    out << " " << dockyard_id;
  }
  out << " ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const SampleStreamsResponse& response) {
  out << "SampleStreamsResponse {" << std::endl;
  out << "  RequestId: " << response.RequestId() << std::endl;
  out << "  lowest_value: " << response.lowest_value << std::endl;
  out << "  highest_value: " << response.highest_value << std::endl;
  out << "  data_sets (" << response.data_sets.size() << "): [" << std::endl;
  for (const auto& list : response.data_sets) {
    out << "    data_set: {";
    for (const auto& [key, value] : list) {
      out << " (" << key << ", " << value << ")";
    }
    out << " }, " << std::endl;
  }
  out << "  ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out, const Dockyard& dockyard) {
  out << "Dockyard {" << std::endl;
  out << "  sample_stream: {";
  for (const auto& stream : dockyard.sample_streams_) {
    out << "    " << stream.first << " (" << stream.second->size() << "): {";
    for (const auto& sample : *stream.second) {
      out << " " << sample.second;
    }
    out << " }, " << std::endl;
  }
  out << " }, " << std::endl;
  out << "}" << std::endl;
  return out;
}

Dockyard::Dockyard()
    : grpc_server_port_(-1),
      device_time_delta_ns_(0ULL),
      latest_sample_time_ns_(0ULL),
      on_connection_handler_(nullptr),
      on_paths_handler_(nullptr) {}

Dockyard::~Dockyard() { StopCollectingFromDevice(); }

SampleTimeNs Dockyard::DeviceDeltaTimeNs() const {
  return device_time_delta_ns_;
}

void Dockyard::SetDeviceTimeDeltaNs(SampleTimeNs delta_ns) {
  device_time_delta_ns_ = delta_ns;
}

SampleTimeNs Dockyard::LatestSampleTimeNs() const {
  return latest_sample_time_ns_;
}

void Dockyard::AddSample(DockyardId dockyard_id, Sample sample) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (ignore_dockyard_ids_.find(dockyard_id) != ignore_dockyard_ids_.end()) {
    return;
  }

  // Find or create a sample_stream for this dockyard_id.
  SampleStream& sample_stream = sample_streams_.StreamRef(dockyard_id);
  sample_stream.emplace(sample.time, sample.value);
  latest_sample_time_ns_ = sample.time;

  // Track the overall lowest and highest values encountered.
  sample_stream_low_high_.try_emplace(dockyard_id,
                                      std::make_pair(SAMPLE_MAX_VALUE, 0ULL));
  auto low_high = sample_stream_low_high_.find(dockyard_id);
  SampleValue lowest = low_high->second.first;
  SampleValue highest = low_high->second.second;
  bool change = false;
  if (lowest > sample.value) {
    lowest = sample.value;
    change = true;
  }
  if (highest < sample.value) {
    highest = sample.value;
    change = true;
  }
  if (change) {
    sample_stream_low_high_[dockyard_id] = std::make_pair(lowest, highest);
  }
}

void Dockyard::AddSamples(DockyardId dockyard_id,
                          const std::vector<Sample>& samples) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (ignore_dockyard_ids_.find(dockyard_id) != ignore_dockyard_ids_.end()) {
    return;
  }

  // Find or create a sample_stream for this dockyard_id.
  SampleStream& sample_stream = sample_streams_.StreamRef(dockyard_id);

  // Track the overall lowest and highest values encountered.
  sample_stream_low_high_.try_emplace(dockyard_id,
                                      std::make_pair(SAMPLE_MAX_VALUE, 0ULL));
  auto low_high = sample_stream_low_high_.find(dockyard_id);
  SampleValue lowest = low_high->second.first;
  SampleValue highest = low_high->second.second;
  for (const auto& sample : samples) {
    if (lowest > sample.value) {
      lowest = sample.value;
    }
    if (highest < sample.value) {
      highest = sample.value;
    }
    sample_stream.emplace(sample.time, sample.value);
  }
  sample_stream_low_high_[dockyard_id] = std::make_pair(lowest, highest);
}

void Dockyard::DiscardSamples(DiscardSamplesRequest&& request,
                              OnDiscardSamplesCallback callback) {
  std::lock_guard<std::mutex> guard(mutex_);
  pending_discard_requests_owned_.emplace_back(request, std::move(callback));
}

DockyardId Dockyard::GetDockyardId(const std::string& dockyard_path) {
  std::lock_guard<std::mutex> guard(mutex_);
  return GetDockyardIdLocked(dockyard_path);
}

bool Dockyard::HasDockyardPath(const std::string& dockyard_path,
                               DockyardId* dockyard_id) const {
  std::lock_guard<std::mutex> guard(mutex_);
  auto search = dockyard_path_to_id_.find(dockyard_path);
  if (search != dockyard_path_to_id_.end()) {
    *dockyard_id = search->second;
    return true;
  }
  *dockyard_id = INVALID_DOCKYARD_ID;
  return false;
}

bool Dockyard::GetDockyardPath(DockyardId dockyard_id,
                               std::string* dockyard_path) const {
  std::lock_guard<std::mutex> guard(mutex_);
  auto search = dockyard_id_to_path_.find(dockyard_id);
  if (search != dockyard_id_to_path_.end()) {
    *dockyard_path = search->second;
    return true;
  }
  dockyard_path->clear();
  return false;
}

DockyardPathToIdMap Dockyard::MatchPaths(const std::string& starting,
                                         const std::string& ending) const {
  std::lock_guard<std::mutex> guard(mutex_);
  return MatchPathsLocked(starting, ending);
}

DockyardPathToIdMap Dockyard::MatchPathsLocked(
    const std::string& starting, const std::string& ending) const {
  DockyardPathToIdMap result;
  DockyardPathToIdMap::const_iterator lower;
  DockyardPathToIdMap::const_iterator upper;
  // Begin with all the paths that match |starting|.
  if (starting.empty()) {
    lower = dockyard_path_to_id_.begin();
    upper = dockyard_path_to_id_.end();
  } else {
    lower = dockyard_path_to_id_.lower_bound(starting);
    if (lower == dockyard_path_to_id_.end()) {
      // Not found, return empty result.
      return result;
    }
    std::string limit = starting;
    limit.back() = limit.back() + 1;
    upper = dockyard_path_to_id_.lower_bound(limit);
  }
  // Filter down to those paths that match |ending|.
  if (ending.empty()) {
    result.insert(lower, upper);
  } else {
    for (; lower != upper; ++lower) {
      if (StringEndsWith(lower->first, ending)) {
        result.insert(*lower);
      }
    }
  }
  return result;
}

void Dockyard::ResetHarvesterData() {
  std::lock_guard<std::mutex> guard(mutex_);
  device_time_delta_ns_ = 0;
  latest_sample_time_ns_ = 0;

  // Maybe send error responses.
  pending_get_requests_owned_.clear();
  pending_discard_requests_owned_.clear();

  ignore_streams_.clear();
  ignore_dockyard_ids_.clear();
  sample_streams_.clear();
  sample_stream_low_high_.clear();

  dockyard_path_to_id_.clear();
  dockyard_id_to_path_.clear();

  // The ID of the invalid value is zero because it's the first value created.
  DockyardId dockyard_id = GetDockyardIdLocked("<INVALID>");
  // The test below should never fail (unless there's a bug).
  if (dockyard_id != INVALID_DOCKYARD_ID) {
    GT_LOG(ERROR) << "INVALID_DOCKYARD_ID string allocation failed. Exiting.";
    exit(1);
  }
}

void Dockyard::GetStreamSets(StreamSetsRequest&& request,
                             OnStreamSetsCallback callback) {
  std::lock_guard<std::mutex> guard(mutex_);
  pending_get_requests_owned_.emplace_back(request, std::move(callback));
}

void Dockyard::GetSampleStreams(SampleStreamsRequest&& request,
                                OnSampleStreamsCallback callback) {
  std::lock_guard<std::mutex> guard(mutex_);
  pending_raw_get_requests_owned_.emplace_back(request, std::move(callback));
}

void Dockyard::IgnoreSamples(IgnoreSamplesRequest&& request,
                             IgnoreSamplesCallback callback) {
  std::lock_guard<std::mutex> guard(mutex_);
  pending_ignore_samples_owned_.emplace_back(request, std::move(callback));
}

void Dockyard::OnConnection(MessageType message_type,
                            uint32_t harvester_version) {
  if (on_connection_handler_ != nullptr) {
    ConnectionResponse response(DOCKYARD_VERSION, harvester_version);
    response.SetMessageType(message_type);
    response.SetRequestId(on_connection_request_.RequestId());
    on_connection_handler_(on_connection_request_, response);
    on_connection_request_ = {};
    on_connection_handler_ = nullptr;
  }
}

bool Dockyard::StartCollectingFrom(ConnectionRequest&& request,
                                   OnConnectionCallback callback,
                                   std::string server_address) {
  if (server_thread_.joinable()) {
    return false;
  }
  ResetHarvesterData();
  if (!Initialize(server_address)) {
    return false;
  }
  on_connection_request_ = request;
  on_connection_handler_ = callback;
  server_thread_ = std::thread([this]() { RunGrpcServer(); });
  GT_LOG(INFO) << "Starting collecting from " << request.DeviceName();
  // TODO(fxbug.dev/39): Connect to the device and start the harvester.
  return server_thread_.joinable();
}

void Dockyard::StopCollectingFromDevice() {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!server_thread_.joinable()) {
    return;
  }
  GT_LOG(INFO) << "Stop collecting from Harvester";
  grpc_server_->Shutdown();
  server_thread_.join();
  grpc_server_.reset();
  protocol_buffer_service_.reset();
}

void Dockyard::IgnoreSamplesLocked(const std::string& starting,
                                   const std::string& ending) {
  // If someone in the future sends repeated requests to ignore the same samples
  // this would be a good place to detect and decline subsequent requests.
  // Repeated calls are harmless and not expected. So that check is not being
  // made.
  ignore_streams_.emplace(std::make_pair(starting, ending));
  DockyardPathToIdMap matches = MatchPathsLocked(starting, ending);
  for (const auto& match : matches) {
    ignore_dockyard_ids_.emplace(match.second);
  }
}

bool Dockyard::Initialize(std::string server_address) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (server_thread_.joinable()) {
    GT_LOG(INFO) << "Dockyard server already initialized";
    return true;
  }

  GT_LOG(INFO) << "Starting dockyard server";
  protocol_buffer_service_ = std::make_unique<DockyardServiceImpl>();

  protocol_buffer_service_->SetDockyard(this);

  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(),
                           &grpc_server_port_);
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to a *synchronous* service. The
  // builder (and server) will hold a weak pointer to the service.
  builder.RegisterService(protocol_buffer_service_.get());
  // Finally assemble the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (grpc_server_port_ <= 0) {
    GT_LOG(ERROR) << "Error binding the gRPC server to the port";
    return false;
  } else if (server.get() == nullptr) {
    // All other start errors are flagged by a null server.
    GT_LOG(ERROR) << "Error starting the gRPC server";
    return false;
  }
  grpc_server_ = std::move(server);
  GT_LOG(INFO) << "Server listening on " << server_address;
  return true;
}

void Dockyard::RunGrpcServer() {
  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  grpc_server_->Wait();
}

OnPathsCallback Dockyard::SetDockyardPathsHandler(OnPathsCallback callback) {
  assert(!server_thread_.joinable());
  auto old_handler = on_paths_handler_;
  on_paths_handler_ = std::move(callback);
  return old_handler;
}

void Dockyard::ProcessDiscardSamples(const DiscardSamplesRequest& request,
                                     DiscardSamplesResponse* response) {
  std::lock_guard<std::mutex> guard(mutex_);
  response->SetRequestId(request.RequestId());
  for (DockyardId dockyard_id : request.dockyard_ids) {
    auto search = sample_streams_.find(dockyard_id);
    if (search == sample_streams_.end()) {
      // No work to do. (Not an error.)
      continue;
    }
    SampleStream* sample_stream = search->second.get();
    auto begin = sample_stream->lower_bound(request.start_time_ns);
    if (begin == sample_stream->end()) {
      // No work to do. (Not an error.)
      continue;
    }
    auto end = sample_stream->lower_bound(request.end_time_ns);
    // If the end time is not found, delete to the end of what we have.
    sample_stream->erase(begin, end);
  }
  // Note: Do not remove the entries from |dockyard_id_to_path_| and
  //       |dockyard_path_to_id_| since those may be sync'd with a remote
  //       process. E.g. the Harvester.
  //
  // Note: Do not alter the values in |sample_stream_low_high_|. Doing so would
  //       cause (GUI) normalization issues when trimming old data. The low/high
  //       information is about the entire history of the sample stream, not any
  //       particular range of time. If clearing or recalculating the low/high
  //       values becomes desirable, do so with a flag in DiscardSamplesRequest
  //       or create a new type of request.
}

void Dockyard::ProcessIgnoreSamples(const IgnoreSamplesRequest& request,
                                    IgnoreSamplesResponse* response) {
  std::lock_guard<std::mutex> guard(mutex_);
  response->SetRequestId(request.RequestId());
  IgnoreSamplesLocked(request.prefix, request.suffix);
}

void Dockyard::ProcessSingleSampleStreamsRequest(
    const SampleStreamsRequest& request,
    SampleStreamsResponse* response) const {
  std::lock_guard<std::mutex> guard(mutex_);
  response->SetRequestId(request.RequestId());
  for (const auto& dockyard_id : request.dockyard_ids) {
    auto search = sample_streams_.find(dockyard_id);
    if (search == sample_streams_.end()) {
      response->data_sets.push_back({});
    } else {
      SampleStream& sample_stream = *search->second;
      response->data_sets.emplace_back(
          sample_stream.lower_bound(request.start_time_ns),
          sample_stream.lower_bound(request.end_time_ns));
    }
  }
  ComputeLowestHighestForSampleStreamsRequest(request, response);
}

void Dockyard::ComputeLowestHighestForSampleStreamsRequest(
    const SampleStreamsRequest& request,
    SampleStreamsResponse* response) const {
  // Gather the overall lowest and highest values encountered.
  SampleValue lowest = SAMPLE_MAX_VALUE;
  SampleValue highest = 0ULL;
  for (const auto& dockyard_id : request.dockyard_ids) {
    auto low_high = sample_stream_low_high_.find(dockyard_id);
    if (low_high == sample_stream_low_high_.end()) {
      continue;
    }
    if (lowest > low_high->second.first) {
      lowest = low_high->second.first;
    }
    if (highest < low_high->second.second) {
      highest = low_high->second.second;
    }
  }
  response->lowest_value = lowest;
  response->highest_value = highest;
}

void Dockyard::ProcessSingleRequest(const StreamSetsRequest& request,
                                    StreamSetsResponse* response) const {
  std::lock_guard<std::mutex> guard(mutex_);
  response->SetRequestId(request.RequestId());
  for (const auto& dockyard_id : request.dockyard_ids) {
    std::vector<SampleValue> samples;
    auto search = sample_streams_.find(dockyard_id);
    if (search == sample_streams_.end()) {
      samples.push_back(NO_STREAM);
    } else {
      SampleStream& sample_stream = *search->second;
      switch (request.render_style) {
        case StreamSetsRequest::SCULPTING:
          ComputeSculpted(dockyard_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::WIDE_SMOOTHING:
          ComputeSmoothed(dockyard_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::LOWEST_PER_COLUMN:
          ComputeLowestPerColumn(dockyard_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::HIGHEST_PER_COLUMN:
          ComputeHighestPerColumn(dockyard_id, sample_stream, request,
                                  &samples);
          break;
        case StreamSetsRequest::AVERAGE_PER_COLUMN:
          ComputeAveragePerColumn(dockyard_id, sample_stream, request,
                                  &samples);
          break;
        case StreamSetsRequest::RECENT:
          ComputeRecent(dockyard_id, sample_stream, request, &samples);
          break;
        default:
          break;
      }
      if (request.HasFlag(StreamSetsRequest::NORMALIZE)) {
        NormalizeResponse(dockyard_id, sample_stream, request, &samples);
      }
    }
    response->data_sets.push_back(samples);
  }
  ComputeLowestHighestForRequest(request, response);
}

void Dockyard::ComputeAveragePerColumn(
    DockyardId /*dockyard_id*/, const SampleStream& sample_stream,
    const StreamSetsRequest& request, std::vector<SampleValue>* samples) const {
  // To calculate the slope, a range of time is needed. |prior_time| and
  // |start_time| define that range. The very first |prior_time| is one stride
  // prior to the requested start time.
  SampleTimeNs prior_time = CalcTimeForStride(request, -1);
  SampleValue prior_value = 0ULL;
  const int64_t limit = request.sample_count;

  for (int64_t sample_n = -1; sample_n < limit; ++sample_n) {
    SampleTimeNs start_time = CalcTimeForStride(request, sample_n);
    SampleTimeNs end_time = CalcTimeForStride(request, sample_n + 1);

    auto begin = sample_stream.lower_bound(start_time);
    if (begin == sample_stream.end()) {
      if (sample_n >= 0) {
        samples->push_back(NO_DATA);
      }
      continue;
    }
    auto end = sample_stream.lower_bound(end_time);
    SampleValue accumulator = 0ULL;
    uint_fast32_t count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      accumulator += i->second;
      ++count;
    }
    SampleValue result = NO_DATA;
    if (count) {
      if (request.HasFlag(StreamSetsRequest::SLOPE)) {
        result = calculate_slope(accumulator / count, &prior_value, start_time,
                                 &prior_time);
      } else {
        result = accumulator / count;
      }
    }
    if (sample_n >= 0) {
      samples->push_back(result);
    }
  }
}

void Dockyard::ComputeHighestPerColumn(
    DockyardId /*dockyard_id*/, const SampleStream& sample_stream,
    const StreamSetsRequest& request, std::vector<SampleValue>* samples) const {
  // To calculate the slope, a range of time is needed. |prior_time| and
  // |start_time| define that range. The very first |prior_time| is one stride
  // prior to the requested start time.
  SampleTimeNs prior_time = CalcTimeForStride(request, -1);
  SampleValue prior_value = 0ULL;
  const int64_t limit = request.sample_count;
  for (int64_t sample_n = -1; sample_n < limit; ++sample_n) {
    SampleTimeNs start_time = CalcTimeForStride(request, sample_n);
    SampleTimeNs end_time = CalcTimeForStride(request, sample_n + 1);

    auto begin = sample_stream.lower_bound(start_time);
    if (begin == sample_stream.end()) {
      if (sample_n >= 0) {
        samples->push_back(NO_DATA);
      }
      continue;
    }
    auto end = sample_stream.lower_bound(end_time);
    SampleTimeNs high_time = request.start_time_ns;
    SampleValue highest = 0ULL;
    uint_fast32_t count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      if (highest < i->second) {
        high_time = i->first;
        highest = i->second;
      }
      ++count;
    }
    SampleValue result = NO_DATA;
    if (count) {
      if (request.HasFlag(StreamSetsRequest::SLOPE)) {
        result = calculate_slope(highest, &prior_value, high_time, &prior_time);
      } else {
        result = highest;
      }
    }
    if (sample_n >= 0) {
      samples->push_back(result);
    }
  }
}

void Dockyard::ComputeLowestPerColumn(DockyardId /*dockyard_id*/,
                                      const SampleStream& sample_stream,
                                      const StreamSetsRequest& request,
                                      std::vector<SampleValue>* samples) const {
  // To calculate the slope, a range of time is needed. |prior_time| and
  // |start_time| define that range. The very first |prior_time| is one stride
  // prior to the requested start time.
  SampleTimeNs prior_time = CalcTimeForStride(request, -1);
  SampleValue prior_value = 0ULL;
  const int64_t limit = request.sample_count;
  for (int64_t sample_n = -1; sample_n < limit; ++sample_n) {
    SampleTimeNs start_time = CalcTimeForStride(request, sample_n);
    auto begin = sample_stream.lower_bound(start_time);
    if (begin == sample_stream.end()) {
      if (sample_n >= 0) {
        samples->push_back(NO_DATA);
      }
      continue;
    }
    SampleTimeNs end_time = CalcTimeForStride(request, sample_n + 1);
    auto end = sample_stream.lower_bound(end_time);
    SampleTimeNs low_time = request.start_time_ns;
    SampleValue lowest = SAMPLE_MAX_VALUE;
    uint_fast32_t count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      if (lowest > i->second) {
        low_time = i->first;
        lowest = i->second;
      }
      ++count;
    }
    SampleValue result = NO_DATA;
    if (count) {
      if (request.HasFlag(StreamSetsRequest::SLOPE)) {
        result = calculate_slope(lowest, &prior_value, low_time, &prior_time);
      } else {
        result = lowest;
      }
    }
    if (sample_n >= 0) {
      samples->push_back(result);
    }
  }
}

void Dockyard::ComputeRecent(DockyardId /*dockyard_id*/,
                             const SampleStream& sample_stream,
                             const StreamSetsRequest& request,
                             std::vector<SampleValue>* samples) const {
  // To calculate the slope, a range of time is needed. |prior_time| and
  // |start_time| define that range. The very first |prior_time| is one stride
  // prior to the requested start time.
  SampleTimeNs prior_time = CalcTimeForStride(request, -1);
  SampleValue prior_value = 0ULL;
  const int64_t limit = request.sample_count;
  for (int64_t sample_n = -1; sample_n < limit; ++sample_n) {
    SampleTimeNs start_time = CalcTimeForStride(request, sample_n);
    SampleTimeNs end_time = CalcTimeForStride(request, sample_n + 1);

    auto begin = sample_stream.lower_bound(start_time);
    if (begin == sample_stream.end()) {
      if (sample_n >= 0) {
        samples->push_back(NO_DATA);
      }
      continue;
    }
    auto end = sample_stream.lower_bound(end_time);
    SampleTimeNs recent_time = request.start_time_ns;
    SampleValue result = NO_DATA;
    if (begin != end) {
      --end;
      recent_time = end->first;
      SampleValue recent_value = end->second;
      if (request.HasFlag(StreamSetsRequest::SLOPE)) {
        result = calculate_slope(recent_value, &prior_value, recent_time,
                                 &prior_time);
      } else {
        result = recent_value;
      }
    }
    if (sample_n >= 0) {
      samples->push_back(result);
    }
  }
}

void Dockyard::NormalizeResponse(DockyardId dockyard_id,
                                 const SampleStream& /*sample_stream*/,
                                 const StreamSetsRequest& /*request*/,
                                 std::vector<SampleValue>* samples) const {
  auto low_high = sample_stream_low_high_.find(dockyard_id);
  SampleValue lowest = low_high->second.first;
  SampleValue highest = low_high->second.second;
  SampleValue value_range = highest - lowest;
  if (value_range == 0) {
    // If there is no range, then all the values drop to zero.
    // Also avoid divide by zero in the code below.
    std::fill(samples->begin(), samples->end(), 0);
    return;
  }

  for (auto& sample : *samples) {
    sample = (sample - lowest) * NORMALIZATION_RANGE / value_range;
  }
}

void Dockyard::ComputeSculpted(DockyardId dockyard_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const {
  // To calculate the slope, a range of time is needed. |prior_time| and
  // |start_time| define that range. The very first |prior_time| is one stride
  // prior to the requested start time.
  SampleTimeNs prior_time = CalcTimeForStride(request, -1);
  SampleValue prior_value = 0ULL;
  auto overall_average = OverallAverageForStream(dockyard_id);
  const int64_t limit = request.sample_count;
  for (int64_t sample_n = -1; sample_n < limit; ++sample_n) {
    SampleTimeNs start_time = CalcTimeForStride(request, sample_n);
    auto begin = sample_stream.lower_bound(start_time);
    if (begin == sample_stream.end()) {
      if (sample_n >= 0) {
        samples->push_back(NO_DATA);
      }
      continue;
    }
    SampleTimeNs end_time = CalcTimeForStride(request, sample_n + 1);
    auto end = sample_stream.lower_bound(end_time);
    SampleValue accumulator = 0ULL;
    SampleValue highest = 0ULL;
    SampleValue lowest = SAMPLE_MAX_VALUE;
    uint_fast32_t count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      auto value = i->second;
      accumulator += value;
      if (highest < value) {
        highest = value;
      }
      if (lowest > value) {
        lowest = value;
      }
      ++count;
    }
    SampleValue result = NO_DATA;
    if (count) {
      auto average = accumulator / count;
      auto final = average >= overall_average ? highest : lowest;
      if (request.HasFlag(StreamSetsRequest::SLOPE)) {
        result = calculate_slope(final, &prior_value, end_time, &prior_time);
      } else {
        result = final;
      }
    }
    if (sample_n >= 0) {
      samples->push_back(result);
    }
  }
}

void Dockyard::ComputeSmoothed(DockyardId /*dockyard_id*/,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const {
  SampleTimeNs prior_time = CalcTimeForStride(request, -1);
  SampleValue prior_value = 0ULL;
  const int64_t limit = request.sample_count;
  for (int64_t sample_n = -1; sample_n < limit; ++sample_n) {
    SampleTimeNs start_time = CalcTimeForStride(request, sample_n - 1);
    auto begin = sample_stream.lower_bound(start_time);
    if (begin == sample_stream.end()) {
      if (sample_n >= 0) {
        samples->push_back(NO_DATA);
      }
      continue;
    }
    SampleTimeNs end_time = CalcTimeForStride(request, sample_n + 2);
    auto end = sample_stream.lower_bound(end_time);
    SampleValue accumulator = 0ULL;
    uint_fast32_t count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      accumulator += i->second;
      ++count;
    }
    SampleValue result = NO_DATA;
    if (count) {
      result = accumulator / count;
      if (request.HasFlag(StreamSetsRequest::SLOPE)) {
        result = calculate_slope(result, &prior_value, end_time, &prior_time);
      }
    }
    if (sample_n >= 0) {
      samples->push_back(result);
    }
  }
}

DockyardId Dockyard::GetDockyardIdLocked(const std::string& dockyard_path) {
  auto search = dockyard_path_to_id_.find(dockyard_path);
  if (search != dockyard_path_to_id_.end()) {
    return search->second;
  }
  DockyardId id = dockyard_path_to_id_.size();
  dockyard_path_to_id_.emplace(dockyard_path, id);
  dockyard_id_to_path_.emplace(id, dockyard_path);

  // Check whether the new path matches any in the ignore list. This is a
  // potentially expensive operation, but the number of ignore elements should
  // be small / reasonable.
  for (const auto& ignore : ignore_streams_) {
    if (StringBeginsWith(dockyard_path, ignore.first) &&
        StringEndsWith(dockyard_path, ignore.second)) {
      ignore_dockyard_ids_.emplace(id);
    }
  }

  return id;
}

SampleValue Dockyard::OverallAverageForStream(DockyardId dockyard_id) const {
  auto low_high = sample_stream_low_high_.find(dockyard_id);
  if (low_high == sample_stream_low_high_.end()) {
    return NO_DATA;
  }
  return (low_high->second.first + low_high->second.second) / 2;
}

void Dockyard::ComputeLowestHighestForRequest(
    const StreamSetsRequest& request, StreamSetsResponse* response) const {
  if (request.HasFlag(StreamSetsRequest::SLOPE)) {
    // Slope responses have fixed low/high values.
    response->lowest_value = 0ULL;
    response->highest_value = SLOPE_LIMIT;
    return;
  }
  // Gather the overall lowest and highest values encountered.
  SampleValue lowest = SAMPLE_MAX_VALUE;
  SampleValue highest = 0ULL;
  for (const auto& dockyard_id : request.dockyard_ids) {
    auto low_high = sample_stream_low_high_.find(dockyard_id);
    if (low_high == sample_stream_low_high_.end()) {
      continue;
    }
    if (lowest > low_high->second.first) {
      lowest = low_high->second.first;
    }
    if (highest < low_high->second.second) {
      highest = low_high->second.second;
    }
  }
  response->lowest_value = lowest;
  response->highest_value = highest;
}

void Dockyard::ProcessRequests() {
  {
    StreamSetsResponse response;
    for (const auto& [request, callback] : pending_get_requests_owned_) {
      ProcessSingleRequest(request, &response);
      callback(request, response);
    }
    pending_get_requests_owned_.clear();
  }

  {
    SampleStreamsResponse response;
    for (const auto& [request, callback] : pending_raw_get_requests_owned_) {
      ProcessSingleSampleStreamsRequest(request, &response);
      callback(request, response);
    }
    pending_raw_get_requests_owned_.clear();
  }

  {
    IgnoreSamplesResponse response;
    for (const auto& [request, callback] : pending_ignore_samples_owned_) {
      ProcessIgnoreSamples(request, &response);
      callback(request, response);
    }
    pending_ignore_samples_owned_.clear();
  }

  {
    DiscardSamplesResponse response;
    for (const auto& [request, callback] : pending_discard_requests_owned_) {
      ProcessDiscardSamples(request, &response);
      callback(request, response);
    }
    pending_discard_requests_owned_.clear();
  }
}

std::ostringstream Dockyard::DebugDump() const {
  std::lock_guard<std::mutex> guard(mutex_);

  // Local helper function to get Dockyard path strings.
  auto get_dockyard_path = [this](DockyardId dockyard_id) {
    auto search = dockyard_id_to_path_.find(dockyard_id);
    if (search != dockyard_id_to_path_.end()) {
      return search->second;
    }
    const std::string NOT_FOUND("<NotFound>");
    return NOT_FOUND;
  };

  std::ostringstream out;
  out << "Dockyard::DebugDump {" << std::endl;
  out << "  paths strings (" << dockyard_id_to_path_.size() << "): ["
      << std::endl;
  if (dockyard_id_to_path_.size() != dockyard_path_to_id_.size()) {
    out << "    Error: dockyard_id_to_path_.size() != "
           "dockyard_path_to_id_.size()"
        << std::endl;
  } else {
    for (const auto& item : dockyard_id_to_path_) {
      out << "    " << item.first << "=" << item.second << "," << std::endl;
    }
  }
  out << "  ]," << std::endl;
  out << "  sample_streams (" << sample_streams_.size() << "): [" << std::endl;
  for (const auto& stream : sample_streams_) {
    const std::string stream_name = get_dockyard_path(stream.first);
    const auto& sample_list = *stream.second;
    out << "    stream: (" << stream.first << ") " << stream_name << ", "
        << sample_list.size() << " entries {" << std::endl;
    if (!sample_list.empty()) {
      out << "     ";
      // Print the last (most recent) entry.
      auto sample = sample_list.end();
      --sample;
      out << " " << sample->first << ": " << sample->second;
      if (StringEndsWith(stream_name, ":name")) {
        out << "=" << get_dockyard_path(sample->second);
      }
      // Print how many times this entry repeats (in recent past).
      int count = 1;
      auto next = sample;
      while (next != sample_list.begin()) {
        --next;
        if (sample->second != next->second) {
          break;
        }
        --sample;
        ++count;
      }
      out << "     (* " << count << "),";
    }
    out << std::endl << "    }," << std::endl;
  }
  out << "  ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

std::ostringstream DebugPrintQuery(const Dockyard& dockyard,
                                   const StreamSetsRequest& request,
                                   const StreamSetsResponse& response) {
  std::ostringstream out;
  out << "StreamSets Query {" << std::endl;
  if (request.RequestId() != response.RequestId()) {
    out << "  RequestId mismatch: " << request.RequestId() << " vs. "
        << response.RequestId() << std::endl;
    return out;
  }
  out << "  RequestId: " << request.RequestId() << std::endl;
  out << "  start_time_ns: " << request.start_time_ns << std::endl;
  out << "  end_time_ns:   " << request.end_time_ns << std::endl;
  out << "    delta time in seconds: "
      << double(request.end_time_ns - request.start_time_ns) /
             kNanosecondsPerSecond
      << std::endl;
  out << "  sample_count: " << request.sample_count << std::endl;
  out << "  min: " << request.min;
  out << "  max: " << request.max;
  out << "  reserved: " << request.reserved << std::endl;
  out << "  render_style: " << request.render_style;
  out << "  flags: " << request.flags << std::endl;
  out << "  lowest_value: " << response.lowest_value << std::endl;
  out << "  highest_value: " << response.highest_value << std::endl;
  if (request.dockyard_ids.size() != response.data_sets.size()) {
    out << "  data size mismatch: " << request.dockyard_ids.size() << " vs. "
        << response.data_sets.size() << std::endl;
    return out;
  }
  out << "  id:data (" << request.dockyard_ids.size() << "): [" << std::endl;
  auto dockyard_id = request.dockyard_ids.begin();
  auto data_set = response.data_sets.begin();
  for (; data_set != response.data_sets.end(); ++data_set, ++dockyard_id) {
    std::string path;
    dockyard.GetDockyardPath(*dockyard_id, &path);
    out << "    data_set " << *dockyard_id << "=" << path << " {";
    for (const auto& data : *data_set) {
      if (data == NO_DATA) {
        out << " NO_DATA";
      } else {
        out << " " << data;
      }
    }
    out << " }, " << std::endl;
  }
  out << "  ]" << std::endl;
  out << "}" << std::endl;
  return out;
}

}  // namespace dockyard
