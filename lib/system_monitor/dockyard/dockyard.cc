// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

namespace dockyard {

namespace {

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

int64_t AddNumbers(int64_t first, int64_t second) { return first + second; }

Dockyard::Dockyard()
    : _next_context_id(0ULL),
      _stream_name_handler(nullptr),
      _stream_sets_handler(nullptr) {}

Dockyard::~Dockyard() {
  for (SampleStreamMap::iterator i = _sample_streams.begin();
       i != _sample_streams.end(); ++i) {
    delete i->second;
  }
}

void Dockyard::AddSamples(SampleStreamId stream_id,
                          std::vector<Sample> samples) {
  // Find or create a sample_stream for this stream_id.
  SampleStream* sample_stream;
  auto search = _sample_streams.find(stream_id);
  if (search == _sample_streams.end()) {
    sample_stream = new SampleStream();
    _sample_streams.emplace(stream_id, sample_stream);
  } else {
    sample_stream = search->second;
  }

  // Track the overall lowest and highest values encountered.
  _sample_stream_low_high.try_emplace(stream_id,
                                      std::make_pair(SAMPLE_MAX_VALUE, 0ULL));
  auto low_high = _sample_stream_low_high.find(stream_id);
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
  _sample_stream_low_high[stream_id] = std::make_pair(lowest, highest);
}

SampleStreamId Dockyard::GetSampleStreamId(const std::string& name) {
  auto search = _stream_ids.find(name);
  if (search != _stream_ids.end()) {
    return search->second;
  }
  SampleStreamId id = _stream_ids.size();
  _stream_ids.emplace(name, id);
  return id;
}

uint64_t Dockyard::GetStreamSets(StreamSetsRequest* request) {
  request->request_id = _next_context_id;
  _pending_requests.push_back(request);
  ++_next_context_id;
  return request->request_id;
}

StreamNamesCallback Dockyard::ObserveStreamNames(StreamNamesCallback callback) {
  auto old_handler = _stream_name_handler;
  _stream_name_handler = callback;
  return old_handler;
}

StreamSetsCallback Dockyard::ObserveStreamSets(StreamSetsCallback callback) {
  auto old_handler = _stream_sets_handler;
  _stream_sets_handler = callback;
  return old_handler;
}

void Dockyard::ProcessSingleRequest(const StreamSetsRequest& request,
                                    StreamSetsResponse* response) const {
  response->request_id = request.request_id;
  for (auto stream_id = request.stream_ids.begin();
       stream_id != request.stream_ids.end(); ++stream_id) {
    std::vector<SampleValue> samples;
    auto search = _sample_streams.find(*stream_id);
    if (search == _sample_streams.end()) {
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
  auto low_high = _sample_stream_low_high.find(stream_id);
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
  auto low_high = _sample_stream_low_high.find(stream_id);
  if (low_high == _sample_stream_low_high.end()) {
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
    auto low_high = _sample_stream_low_high.find(*stream_id);
    if (low_high == _sample_stream_low_high.end()) {
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
  if (_stream_sets_handler != nullptr) {
    StreamSetsResponse response;
    for (auto i = _pending_requests.begin(); i != _pending_requests.end();
         ++i) {
      ProcessSingleRequest(**i, &response);
      _stream_sets_handler(response);
    }
  }
  _pending_requests.clear();
}

}  // namespace dockyard
