// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../sw/codec_adapter_sw.h"

#include <lib/fit/defer.h>
#include <lib/fit/function.h>

#include "gtest/gtest.h"

class CodecAdapterSWDummy
    : public CodecAdapterSW<fit::deferred_action<fit::closure>> {
 public:
  CodecAdapterSWDummy(std::mutex& lock)
      : CodecAdapterSW(lock,
                       /* bad ptr to pass non-null assert */ reinterpret_cast<
                           CodecAdapterEvents*>(0xaa)) {}

  // Much like the real fit::defer(s) in in_use_by_client_, the fit::defer we're
  // putting in in_use_by_client_ touches output_buffer_pool_ in a way that'll
  // crash if output_buffer_pool_ is already destructed.
  void EntangleClientMapAndBufferPoolDestructors() {
    std::lock_guard<std::mutex> lock(lock_);
    fit::closure deferred = [this]() {
      // This call will crash if output_buffer_pool_ has already been
      // destructed, much like the real-world fit::defer(s) that would be in
      // in_use_by_client_ if we delete CodecAdapterSW with stuff in flight.
      output_buffer_pool_.has_buffers_in_use();
    };
    in_use_by_client_[nullptr] = fit::defer(std::move(deferred));
  }

  fuchsia::sysmem::BufferCollectionConstraints
  CoreCodecGetBufferCollectionConstraints(
      CodecPort port,
      const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
      const fuchsia::media::StreamBufferPartialSettings& partial_settings)
      override {
    return fuchsia::sysmem::BufferCollectionConstraints();
  }

  void CoreCodecSetBufferCollectionInfo(
      CodecPort port,
      const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info)
      override {}

 protected:
  virtual void ProcessInputLoop() override {}

  virtual void CleanUpAfterStream() override {}

  virtual std::pair<fuchsia::media::FormatDetails, size_t> OutputFormatDetails()
      override {
    return {fuchsia::media::FormatDetails(), 0};
  }
};

TEST(CodecAdapterSW, DoesNotCrashOnDestruction) {
  // To pass, this test must not crash.
  std::mutex lock;
  auto under_test = CodecAdapterSWDummy(lock);
}
