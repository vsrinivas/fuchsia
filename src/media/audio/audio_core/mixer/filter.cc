// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/filter.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>

#include <iomanip>
#include <memory>
#include <mutex>

#include "src/media/audio/audio_core/mixer/coefficient_table.h"

namespace media::audio::mixer {

// Display the filter table values.
void Filter::DisplayTable(const CoefficientTable& filter_coefficients) {
  FX_LOGS(INFO) << "Filter: source rate " << source_rate_ << ", dest rate " << dest_rate_
                << ", length 0x" << std::hex << side_length_;

  FX_LOGS(INFO) << " **************************************************************";
  FX_LOGS(INFO) << " *** Displaying filter coefficient data for length " << side_length_ << "  ***";
  FX_LOGS(INFO) << " **************************************************************";

  char str[256];
  str[0] = 0;
  int n;
  for (int64_t idx = 0; idx < side_length_; ++idx) {
    if (idx % 16 == 0) {
      FX_LOGS(INFO) << str;
      n = sprintf(str, " [%5lx] ", idx);
    }
    if (filter_coefficients[idx] < std::numeric_limits<float>::epsilon() &&
        filter_coefficients[idx] > -std::numeric_limits<float>::epsilon() &&
        filter_coefficients[idx] != 0.0f) {
      n += sprintf(str + n, "!%10.7f!", filter_coefficients[idx]);
    } else {
      n += sprintf(str + n, " %10.7f ", filter_coefficients[idx]);
    }
  }
  FX_LOGS(INFO) << str;
  FX_LOGS(INFO) << " **************************************************************";
}

// Used to debug computation of output values (ComputeSample), from coefficients and input values.
constexpr bool kTraceComputation = false;

// For frac_offset in [0.0, 1.0) we require source frames on each side depending on filter length.
// Source frames are at integral positions, but we treat frac_offset as filter center, so source
// frames appear to be fractionally positioned.
//
// Filter coefficients cover the entire discrete space of fractional positions, but any calculation
// references only a subset of these, using a one-frame stride (frac_size_). Coefficient tables
// internally store values with an integer stride contiguously, which is what these loops want.
//   Ex:
//     coefficient_ptr[1] == filter_coefficients[frac_offset + frac_size_];
//
// We first calculate the contribution of the negative side of the filter, and then the contribution
// of the positive side. To avoid double-counting it, we include center subframe 0 only in the
// negative-side calculation.
float Filter::ComputeSampleFromTable(const CoefficientTable& filter_coefficients,
                                     int64_t frac_offset, float* center) {
  FX_DCHECK(frac_offset <= frac_size_) << frac_offset;
  if constexpr (kTraceComputation) {
    FX_LOGS(INFO) << "For frac_offset 0x" << std::hex << frac_offset << " ("
                  << (static_cast<double>(frac_offset) / static_cast<double>(frac_size_)) << "):";
  }

  // We use some raw pointers here to make loops vectorizable.
  float* sample_ptr;
  const float* coefficient_ptr;
  float result = 0.0f;

  // Negative side examples --
  // side_length 1.601, frac_offset 0.600 requires source range (-1.001, 0.600]: frames -1 and 0.
  // side_length 1.601, frac_offset 0.601 requires source range (-1.000, 0.601]: frame 0.
  int64_t source_frames = (side_length_ - 1 + frac_size_ - frac_offset) >> num_frac_bits_;
  if (source_frames > 0) {
    sample_ptr = center;
    coefficient_ptr = filter_coefficients.ReadSlice(frac_offset, source_frames);
    FX_CHECK(coefficient_ptr != nullptr);

    for (int64_t source_idx = 0; source_idx < source_frames; ++source_idx) {
      auto contribution = (*sample_ptr) * coefficient_ptr[source_idx];
      if constexpr (kTraceComputation) {
        FX_LOGS(INFO) << "Adding source[" << -static_cast<ssize_t>(source_idx) << "] "
                      << (*sample_ptr) << " x " << coefficient_ptr[source_idx] << " = "
                      << contribution;
      }
      result += contribution;
      --sample_ptr;
    }
  }

  // Positive side examples --
  // side_length 1.601, frac_offset 0.400 requires source range (0.400, 2.001): frames 1 and 2.
  // side_length 1.601, frac_offset 0.399 requires source range (0.399, 2.000): frame 1.
  //
  // Reduction of: side_length_ + (frac_size_-1) - (frac_size_-frac_offset)
  source_frames = (side_length_ - 1 + frac_offset) >> num_frac_bits_;
  if (source_frames > 0) {
    sample_ptr = center + 1;
    coefficient_ptr = filter_coefficients.ReadSlice(frac_size_ - frac_offset, source_frames);
    FX_CHECK(coefficient_ptr != nullptr);

    for (int64_t source_idx = 0; source_idx < source_frames; ++source_idx) {
      auto contribution = sample_ptr[source_idx] * coefficient_ptr[source_idx];
      if constexpr (kTraceComputation) {
        FX_LOGS(INFO) << "Adding source[" << 1 + source_idx << "] " << std::setprecision(13)
                      << sample_ptr[source_idx] << " x " << coefficient_ptr[source_idx] << " = "
                      << contribution;
      }
      result += contribution;
    }
  }

  if constexpr (kTraceComputation) {
    FX_LOGS(INFO) << "... to get " << std::setprecision(13) << result;
  }
  return result;
}

SincFilter::CacheT* CreateSincFilterCoefficientTableCache() {
  auto cache = new SincFilter::CacheT([](SincFilterCoefficientTable::Inputs inputs) {
    TRACE_DURATION("audio", "CreateSincFilterTable");
    auto start_time = zx::clock::get_monotonic();
    auto t = SincFilterCoefficientTable::Create(inputs);
    auto end_time = zx::clock::get_monotonic();
    FX_LOGS(INFO) << "CreateSincFilterTable took " << (end_time - start_time).to_nsecs()
                  << " ns with Inputs { side_length=" << inputs.side_length
                  << ", num_frac_bits=" << inputs.num_frac_bits
                  << ", rate_conversion_ratio=" << inputs.rate_conversion_ratio << " }";
    return t.release();
  });

  // To avoid lengthy construction time, cache some coefficient tables persistently.
  // See fxbug.dev/45074 and fxbug.dev/57666.
  SincFilter::persistent_cache_ = new std::vector<SincFilter::CacheT::SharedPtr>;

  // First load any coefficient tables that were built into this executable.
  for (auto& t : kPrebuiltSincFilterCoefficientTables) {
    auto inputs = SincFilterCoefficientTable::MakeInputs(t.source_rate, t.dest_rate);
    SincFilter::persistent_cache_->push_back(cache->Add(
        inputs, new CoefficientTable(inputs.side_length, inputs.num_frac_bits, t.table)));
  }

  // Now make sure we have all the coefficient tables we need.
  // In practice, this should be a superset of the prebuilt tables.
  SincFilter::persistent_cache_->push_back(
      cache->Get(SincFilterCoefficientTable::MakeInputs(48000, 48000)));
  SincFilter::persistent_cache_->push_back(
      cache->Get(SincFilterCoefficientTable::MakeInputs(96000, 48000)));
  SincFilter::persistent_cache_->push_back(
      cache->Get(SincFilterCoefficientTable::MakeInputs(48000, 96000)));
  SincFilter::persistent_cache_->push_back(
      cache->Get(SincFilterCoefficientTable::MakeInputs(96000, 16000)));
  SincFilter::persistent_cache_->push_back(
      cache->Get(SincFilterCoefficientTable::MakeInputs(48000, 16000)));
  SincFilter::persistent_cache_->push_back(
      cache->Get(SincFilterCoefficientTable::MakeInputs(44100, 48000)));

  return cache;
}

// static
PointFilter::CacheT* const PointFilter::cache_ =
    new PointFilter::CacheT([](PointFilterCoefficientTable::Inputs inputs) {
      TRACE_DURATION("audio", "CreatePointFilterTable");
      return PointFilterCoefficientTable::Create(inputs).release();
    });

// static
LinearFilter::CacheT* const LinearFilter::cache_ =
    new LinearFilter::CacheT([](LinearFilterCoefficientTable::Inputs inputs) {
      TRACE_DURATION("audio", "CreateLinearFilterTable");
      return LinearFilterCoefficientTable::Create(inputs).release();
    });

// static
// Must initialize persistent_cache_ first as it's used by the Create function.
std::vector<SincFilter::CacheT::SharedPtr>* SincFilter::persistent_cache_ = nullptr;
SincFilter::CacheT* const SincFilter::cache_ = CreateSincFilterCoefficientTableCache();

}  // namespace media::audio::mixer
