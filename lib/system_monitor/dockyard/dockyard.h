// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_H_
#define GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_H_

#include <stdint.h>
#include <map>
#include <string>
#include <vector>

namespace dockyard {

// This is a simple hello world style API call to test with. This will be
// removed once the initial Dart library is implemented.
int64_t AddNumbers(int64_t first, int64_t second);

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

// Special value for missing sample stream.
constexpr SampleValue NO_STREAM = (SampleValue)-1ULL;
// Special value for missing data.
constexpr SampleValue NO_DATA = (SampleValue)-2ULL;
// The highest value for sample data.
constexpr SampleValue SAMPLE_MAX_VALUE = (SampleValue)-3ULL;

// The upper value used to represent zero to one values with integers.
constexpr SampleValue NORMALIZATION_RANGE = 1000000ULL;

// A Sample.
struct Sample {
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

// Settings for calling GenerateRandomSamples() to create test samples.
struct RandomSampleGenerator {
  RandomSampleGenerator()
      : stream_id(0),
        seed(0),
        time_style(TIME_STYLE_LINEAR),
        start(0),
        finish(100),
        value_style(VALUE_STYLE_SINE_WAVE),
        value_min(0),
        value_max(SAMPLE_MAX_VALUE),
        sample_count(100) {}

  // How time should progress.
  enum RandomTimeStyle {
    // Add samples at the same interval, without variance.
    TIME_STYLE_LINEAR,
    // Vary times for samples by a small amount.
    TIME_STYLE_SHORT_STAGGER,
    // Like TIME_STYLE_SHORT_STAGGER, with more variance.
    TIME_STYLE_LONG_STAGGER,
    // Add clumps of samples separated by relatively long absences of samples.
    TIME_STYLE_CLUMPED,
    // Let the generator do whatever it likes.
    TIME_STYLE_OPEN,
  };

  // How values are created.
  enum RandomValueStyle {
    // Start at |min| and go to |max| without decreasing.
    VALUE_STYLE_MONO_INCREASE,
    // Start at |max| and go to |min| without increasing.
    VALUE_STYLE_MONO_DECREASE,
    // Choose random values in the upper quarter of the range, then the lower
    // quarter of the range, and so on.
    VALUE_STYLE_JAGGED,
    // Random values from |min| to |max| for each value.
    VALUE_STYLE_RANDOM,
    // Go a little up or down at each step, staying within |min| and |max|.
    VALUE_STYLE_RANDOM_WALK,
    // Plot a sine wave within |min| and |max|.
    VALUE_STYLE_SINE_WAVE,
  };

  // E.g. as provided by |GetSampleStreamId()| to get an ID value.
  SampleStreamId stream_id;
  // Value used for srand(). Using a consistent seed value will yield
  // predictable samples.
  unsigned int seed;

  // How time should progress.
  RandomTimeStyle time_style;
  // The initial time for this set of samples. The first sample will be created
  // at this time stamp.
  SampleTimeNs start;
  // The end time for this set of samples. This is a guide, the last sample may
  // be a bit shy or exceed this value.
  SampleTimeNs finish;

  // How values are created.
  RandomValueStyle value_style;
  // The lowest value. It's possible that no sample actually has this value,
  // but none will be less than |value_min|.
  SampleValue value_min;
  // The highest value. It's possible that no sample actually has this value,
  // but none will be higher than |value_max|.
  SampleValue value_max;

  // How many samples to create. This will overrule |finish| time. I.e. more
  // samples will be created to satisfy |sample_count| even if that results in
  // going past the |finish| time.
  size_t sample_count;
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
};

// Lookup for a sample stream name string, given the sample stream ID.
typedef std::map<SampleStreamId, std::string> StreamInfoMap;

// Called when new streams are added or removed. Added values include their ID
// and string name. Removed values only have the ID.
// Intended to inform clients of StreamInfoMap changes (so they may keep their
// equivalent map in sync). The racy nature of this update is not an issue
// because the rest of the API will cope with invalid stream IDs, so 'eventually
// consistent' is acceptable).
typedef std::function<void(const std::vector<StreamInfo>& add,
                           const std::vector<SampleStreamId>& remove)>
    StreamNamesCallback;

// Called after (and in response to) a request is sent to |GetStreamSets()|.
typedef std::function<void(const StreamSetsResponse& response)>
    StreamSetsCallback;

class Dockyard {
 public:
  Dockyard();
  ~Dockyard();

  // Insert test samples. This is to assist in testing the GUI. Given the same
  // inputs, the same samples will be generated (i.e. pseudo-random, not truly
  // random).
  void AddRandomSamples(const RandomSampleGenerator& gen);

  // Insert sample information for a given stream_id. Not intended for use by
  // the GUI.
  void AddSamples(SampleStreamId stream_id, std::vector<Sample> samples);

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
  // Returns unique context ID.
  uint64_t GetStreamSets(StreamSetsRequest* request);

  // Listen for sample streams being added or removed. |callback| is called
  // when new streams are added or removed. Pass nullptr as |callback| to stop
  // listening.
  //
  // Returns prior callback or nullptr.
  StreamNamesCallback ObserveStreamNames(StreamNamesCallback callback);

  // Listen for sample streams being added or removed. |callback| is called
  // when new streams are added or removed. Pass nullptr as |callback| to stop
  // listening.
  //
  // Returns prior callback or nullptr.
  StreamSetsCallback ObserveStreamSets(StreamSetsCallback callback);

  // Generate responses and call handlers for sample requests. Not intended for
  // use by the GUI.
  void ProcessRequests();

 private:
  typedef std::map<SampleStreamId, SampleStream*> SampleStreamMap;
  uint64_t _next_context_id;
  StreamNamesCallback _stream_name_handler;
  StreamSetsCallback _stream_sets_handler;
  std::vector<StreamSetsRequest*> _pending_requests;
  SampleStreamMap _sample_streams;
  std::map<SampleStreamId, std::pair<SampleValue, SampleValue>>
      _sample_stream_low_high;
  std::map<std::string, SampleStreamId> _stream_ids;

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

// Insert test samples. This is to assist in testing the GUI. Given the same
// inputs, the same samples will be generated (i.e. pseudo-random, not truly
// random).
void GenerateRandomSamples(const RandomSampleGenerator& gen,
                           Dockyard* dockyard);

}  // namespace dockyard

#endif  // GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_H_
