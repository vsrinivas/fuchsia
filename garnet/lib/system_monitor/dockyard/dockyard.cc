// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

#include <iostream>
#include <memory>
#include <string>

#include <grpc++/grpc++.h>

#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"
#include "lib/fxl/logging.h"

namespace dockyard {

namespace {

// This is an arbitrary default port.
constexpr char DEFAULT_SERVER_ADDRESS[] = "0.0.0.0:50051";

// Logic and data behind the server's behavior.
class DockyardServiceImpl final : public dockyard_proto::Dockyard::Service {
 public:
  void SetDockyard(Dockyard* dockyard) { dockyard_ = dockyard; }

 private:
  Dockyard* dockyard_;

  grpc::Status Init(grpc::ServerContext* context,
                    const dockyard_proto::InitRequest* request,
                    dockyard_proto::InitReply* reply) override {
    if (request->version() != DOCKYARD_VERSION) {
      return grpc::Status::CANCELLED;
    }
    reply->set_version(DOCKYARD_VERSION);
    return grpc::Status::OK;
  }

  // This is the handler for the client sending a `SendSample` message. A better
  // name would be `ReceiveSample` but then it wouldn't match the message
  // name.
  grpc::Status SendSample(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                               dockyard_proto::RawSample>* stream) override {
    dockyard_proto::RawSample sample;
    while (stream->Read(&sample)) {
      FXL_LOG(INFO) << "Received sample at " << sample.time() << ", key "
                    << sample.sample().key() << ": " << sample.sample().value();
    }
    return grpc::Status::OK;
  }

  grpc::Status GetStreamIdForName(
      grpc::ServerContext* context,
      const dockyard_proto::StreamNameMessage* request,
      dockyard_proto::StreamIdMessage* reply) override {
    SampleStreamId stream_id = dockyard_->GetSampleStreamId(request->name());
    reply->set_id(stream_id);
    FXL_LOG(INFO) << "Received StreamNameMessage "
                  << ": " << request->name() << ", id " << stream_id;
    return grpc::Status::OK;
  }
};

// Listen for Harvester connections from the Fuchsia device.
void RunGrpcServer(const char* listen_at, Dockyard* dockyard) {
  std::string server_address(listen_at);
  DockyardServiceImpl service;
  service.SetDockyard(dockyard);

  grpc::ServerBuilder builder;
  // Listen on the given address without any authentication mechanism.
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  // Register "service" as the instance through which we'll communicate with
  // clients. In this case it corresponds to a *synchronous* service.
  builder.RegisterService(&service);
  // Finally assemble the server.
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  FXL_LOG(INFO) << "Server listening on " << server_address;

  // Wait for the server to shutdown. Note that some other thread must be
  // responsible for shutting down the server for this call to ever return.
  server->Wait();
}

// The stride is how much time is in each sample.
constexpr SampleTimeNs CalcStride(SampleTimeNs start, SampleTimeNs finish,
                                  size_t count) {
  SampleTimeNs stride = (finish - start);
  if (count) {
    stride /= count;
  }
  return stride;
}

constexpr SampleTimeNs CalcStride(const StreamSetsRequest& request) {
  return CalcStride(request.start_time_ns, request.end_time_ns,
                    request.sample_count);
}

}  // namespace

Dockyard::Dockyard()
    : server_thread_(nullptr),
      stream_name_handler_(nullptr),
      stream_sets_handler_(nullptr),
      next_context_id_(0ULL) {}

Dockyard::~Dockyard() {
  std::lock_guard<std::mutex> guard(mutex_);
  FXL_LOG(INFO) << "Stopping dockyard server";
  if (server_thread_ != nullptr) {
    server_thread_->Join();
  }
  for (SampleStreamMap::iterator i = sample_streams_.begin();
       i != sample_streams_.end(); ++i) {
    delete i->second;
  }
}

void Dockyard::AddSamples(SampleStreamId stream_id,
                          std::vector<Sample> samples) {
  std::lock_guard<std::mutex> guard(mutex_);
  // Find or create a sample_stream for this stream_id.
  SampleStream* sample_stream;
  auto search = sample_streams_.find(stream_id);
  if (search == sample_streams_.end()) {
    sample_stream = new SampleStream();
    sample_streams_.emplace(stream_id, sample_stream);
  } else {
    sample_stream = search->second;
  }

  // Track the overall lowest and highest values encountered.
  sample_stream_low_high_.try_emplace(stream_id,
                                      std::make_pair(SAMPLE_MAX_VALUE, 0ULL));
  auto low_high = sample_stream_low_high_.find(stream_id);
  SampleValue lowest = low_high->second.first;
  SampleValue highest = low_high->second.second;
  for (auto i = samples.begin(); i != samples.end(); ++i) {
    if (lowest > i->value) {
      lowest = i->value;
    }
    if (highest < i->value) {
      highest = i->value;
    }
    sample_stream->emplace(i->time, i->value);
  }
  sample_stream_low_high_[stream_id] = std::make_pair(lowest, highest);
}

SampleStreamId Dockyard::GetSampleStreamId(const std::string& name) {
  std::lock_guard<std::mutex> guard(mutex_);
  auto search = stream_ids_.find(name);
  if (search != stream_ids_.end()) {
    return search->second;
  }
  SampleStreamId id = stream_ids_.size();
  stream_ids_.emplace(name, id);
  stream_names_.emplace(id, name);
  return id;
}

uint64_t Dockyard::GetStreamSets(StreamSetsRequest* request) {
  std::lock_guard<std::mutex> guard(mutex_);
  request->request_id = next_context_id_;
  pending_requests_.push_back(request);
  ++next_context_id_;
  return request->request_id;
}

void Dockyard::StartCollectingFrom(const std::string& device) {
  Initialize();
  FXL_LOG(INFO) << "Starting collecting from " << device;
  // TODO(dschuyler): Connect to the device and start the harvester.
}

void Dockyard::StopCollectingFrom(const std::string& device) {
  FXL_LOG(INFO) << "Stop collecting from " << device;
  // TODO(dschuyler): Stop the harvester.
}

bool Dockyard::Initialize() {
  if (server_thread_ != nullptr) {
    return true;
  }
  FXL_LOG(INFO) << "Starting dockyard server";
  server_thread_ =
      new fxl::Thread([this] { RunGrpcServer(DEFAULT_SERVER_ADDRESS, this); });
  return server_thread_->Run();
}

StreamNamesCallback Dockyard::SetStreamNamesHandler(
    StreamNamesCallback callback) {
  assert(server_thread_ == nullptr);
  auto old_handler = stream_name_handler_;
  stream_name_handler_ = callback;
  return old_handler;
}

StreamSetsCallback Dockyard::SetStreamSetsHandler(StreamSetsCallback callback) {
  auto old_handler = stream_sets_handler_;
  stream_sets_handler_ = callback;
  return old_handler;
}

void Dockyard::ProcessSingleRequest(const StreamSetsRequest& request,
                                    StreamSetsResponse* response) const {
  std::lock_guard<std::mutex> guard(mutex_);
  response->request_id = request.request_id;
  for (auto stream_id = request.stream_ids.begin();
       stream_id != request.stream_ids.end(); ++stream_id) {
    std::vector<SampleValue> samples;
    auto search = sample_streams_.find(*stream_id);
    if (search == sample_streams_.end()) {
      samples.push_back(NO_STREAM);
    } else {
      auto sample_stream = *search->second;
      switch (request.render_style) {
        case StreamSetsRequest::SCULPTING:
          ComputeSculpted(*stream_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::WIDE_SMOOTHING:
          ComputeSmoothed(*stream_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::LOWEST_PER_COLUMN:
          ComputeLowestPerColumn(*stream_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::HIGHEST_PER_COLUMN:
          ComputeHighestPerColumn(*stream_id, sample_stream, request, &samples);
          break;
        case StreamSetsRequest::AVERAGE_PER_COLUMN:
          ComputeAveragePerColumn(*stream_id, sample_stream, request, &samples);
          break;
        default:
          break;
      }
      if ((request.flags & StreamSetsRequest::NORMALIZE) != 0) {
        NormalizeResponse(*stream_id, sample_stream, request, &samples);
      }
    }
    response->data_sets.push_back(samples);
  }
  ComputeLowestHighestForRequest(request, response);
}

void Dockyard::ComputeAveragePerColumn(
    SampleStreamId stream_id, const SampleStream& sample_stream,
    const StreamSetsRequest& request, std::vector<SampleValue>* samples) const {
  // The stride is how much time is in each sample.
  SampleTimeNs stride = (request.end_time_ns - request.start_time_ns);
  if (request.sample_count) {
    stride /= request.sample_count;
  }
  for (uint64_t sample_n = 0; sample_n < request.sample_count; ++sample_n) {
    auto start_time = request.start_time_ns + sample_n * stride;
    auto begin = sample_stream.lower_bound(start_time);
    auto end = sample_stream.upper_bound(start_time + stride);
    SampleValue accumulator = 0ULL;
    SampleValue count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      accumulator += i->second;
      ++count;
    }
    if (count) {
      samples->push_back(accumulator / count);
    } else {
      samples->push_back(-2ULL);
    }
  }
}

void Dockyard::ComputeHighestPerColumn(
    SampleStreamId stream_id, const SampleStream& sample_stream,
    const StreamSetsRequest& request, std::vector<SampleValue>* samples) const {
  SampleTimeNs stride = CalcStride(request);
  for (uint64_t sample_n = 0; sample_n < request.sample_count; ++sample_n) {
    auto start_time = request.start_time_ns + sample_n * stride;
    auto begin = sample_stream.lower_bound(start_time);
    auto end = sample_stream.upper_bound(start_time + stride);
    SampleValue highest = 0ULL;
    SampleValue count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      if (highest < i->second) {
        highest = i->second;
      }
      ++count;
    }
    if (count) {
      samples->push_back(highest);
    } else {
      samples->push_back(-2ULL);
    }
  }
}

void Dockyard::ComputeLowestPerColumn(SampleStreamId stream_id,
                                      const SampleStream& sample_stream,
                                      const StreamSetsRequest& request,
                                      std::vector<SampleValue>* samples) const {
  SampleTimeNs stride = CalcStride(request);
  for (uint64_t sample_n = 0; sample_n < request.sample_count; ++sample_n) {
    auto start_time = request.start_time_ns + sample_n * stride;
    auto begin = sample_stream.lower_bound(start_time);
    auto end = sample_stream.upper_bound(start_time + stride);
    SampleValue lowest = SAMPLE_MAX_VALUE;
    SampleValue count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      if (lowest > i->second) {
        lowest = i->second;
      }
      ++count;
    }
    if (count) {
      samples->push_back(lowest);
    } else {
      samples->push_back(-2ULL);
    }
  }
}

void Dockyard::NormalizeResponse(SampleStreamId stream_id,
                                 const SampleStream& sample_stream,
                                 const StreamSetsRequest& request,
                                 std::vector<SampleValue>* samples) const {
  auto low_high = sample_stream_low_high_.find(stream_id);
  SampleValue lowest = low_high->second.first;
  SampleValue highest = low_high->second.second;
  SampleValue value_range = highest - lowest;
  if (value_range == 0) {
    // If there is no range, then all the values drop to zero.
    // Also avoid divide by zero in the code below.
    std::fill(samples->begin(), samples->end(), 0);
    return;
  }

  for (std::vector<SampleValue>::iterator i = samples->begin();
       i != samples->end(); ++i) {
    *i = (*i - lowest) * NORMALIZATION_RANGE / value_range;
  }
}

void Dockyard::ComputeSculpted(SampleStreamId stream_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const {
  SampleTimeNs stride = CalcStride(request);
  auto overall_average = OverallAverageForStream(stream_id);
  for (uint64_t sample_n = 0; sample_n < request.sample_count; ++sample_n) {
    auto start_time = request.start_time_ns + sample_n * stride;
    auto begin = sample_stream.lower_bound(start_time);
    auto end = sample_stream.upper_bound(start_time + stride);
    SampleValue accumulator = 0ULL;
    SampleValue highest = 0ULL;
    SampleValue lowest = SAMPLE_MAX_VALUE;
    SampleValue count = 0ULL;
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
    if (count) {
      auto average = accumulator / count;
      auto final = average >= overall_average ? highest : lowest;
      samples->push_back(final);
    } else {
      samples->push_back(-2ULL);
    }
  }
}

void Dockyard::ComputeSmoothed(SampleStreamId stream_id,
                               const SampleStream& sample_stream,
                               const StreamSetsRequest& request,
                               std::vector<SampleValue>* samples) const {
  SampleTimeNs stride = CalcStride(request);
  for (uint64_t sample_n = 0; sample_n < request.sample_count; ++sample_n) {
    auto start_time = request.start_time_ns + sample_n * stride;
    auto begin = sample_stream.lower_bound(start_time - stride);
    auto end = sample_stream.upper_bound(start_time + stride * 2);
    SampleValue accumulator = 0ULL;
    SampleValue count = 0ULL;
    for (auto i = begin; i != end; ++i) {
      accumulator += i->second;
      ++count;
    }
    if (count) {
      samples->push_back(accumulator / count);
    } else {
      samples->push_back(-2ULL);
    }
  }
}

SampleValue Dockyard::OverallAverageForStream(SampleStreamId stream_id) const {
  auto low_high = sample_stream_low_high_.find(stream_id);
  if (low_high == sample_stream_low_high_.end()) {
    return NO_DATA;
  }
  return (low_high->second.first + low_high->second.second) / 2;
}

void Dockyard::ComputeLowestHighestForRequest(
    const StreamSetsRequest& request, StreamSetsResponse* response) const {
  // Gather the overall lowest and highest values encountered.
  SampleValue lowest = SAMPLE_MAX_VALUE;
  SampleValue highest = 0ULL;
  for (auto stream_id = request.stream_ids.begin();
       stream_id != request.stream_ids.end(); ++stream_id) {
    auto low_high = sample_stream_low_high_.find(*stream_id);
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
  if (stream_sets_handler_ != nullptr) {
    StreamSetsResponse response;
    for (auto i = pending_requests_.begin(); i != pending_requests_.end();
         ++i) {
      ProcessSingleRequest(**i, &response);
      stream_sets_handler_(response);
    }
  }
  pending_requests_.clear();
}

}  // namespace dockyard
