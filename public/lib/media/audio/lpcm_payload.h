// Copyright 2017 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdlib>

#include "lib/fxl/compiler_specific.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/media/audio/types.h"

namespace media {

class LpcmOutputStream;

class LpcmPayload {
 public:
  LpcmPayload();

  LpcmPayload(nullptr_t) : LpcmPayload() {}

  LpcmPayload(LpcmPayload&& other);

  ~LpcmPayload();

  AudioSampleFormat sample_format() const {
    return owner_ ? owner_->sample_format() : AudioSampleFormat::NONE;
  }

  uint32_t channel_count() const {
    return owner_ ? owner_->channel_count() : 0;
  }

  uint32_t bytes_per_sample() const { return BytesPerSample(sample_format()); }

  uint32_t bytes_per_frame() const {
    return channel_count() * bytes_per_sample();
  }

  // Returns a pointer to the data owned by this |LpcmPayload|.
  void* data() const { return data_; }

  template <typename SampleType>
  SampleType* samples() const {
    FXL_DCHECK(sample_format() == SampleTypeTraits<SampleType>::kSampleFormat);
    return reinterpret_cast<SampleType*>(data_);
  }

  // Returns the size in bytes of the data owned by this |LpcmPayload|.
  size_t size() const { return size_; }

  size_t frame_count() const { return size_ / bytes_per_frame(); }

  size_t sample_count() const { return size_ / bytes_per_sample(); }

  LpcmPayload& operator=(LpcmPayload&& other);

  explicit operator bool() const { return data_ != nullptr; }

  // Frees the payload buffer managed by this |LpcmPayload|.
  void reset();

  void swap(LpcmPayload& other);

  void FillWithSilence();

  // Equality tests with another |LpcmPayload| are nonsensical.
  bool operator==(const LpcmPayload& p2) const = delete;
  bool operator!=(const LpcmPayload& p2) const = delete;

 private:
  friend class LpcmOutputStream;

  class Owner {
   public:
    virtual ~Owner() {}

    AudioSampleFormat sample_format() const { return sample_format_; }

    uint32_t channel_count() const { return channel_count_; }

    virtual void FreePayloadBuffer(void* buffer) = 0;

   protected:
    Owner(AudioSampleFormat sample_format, uint32_t channel_count)
        : sample_format_(sample_format), channel_count_(channel_count) {}

   private:
    AudioSampleFormat sample_format_;
    uint32_t channel_count_;
  };

  // Constructs a |LpcmPayload|. |data| must be nullptr if and only if |size| is
  // 0.
  LpcmPayload(void* data, size_t size, std::shared_ptr<Owner> owner);

  // Replaces the data owned by this |LpcmPayload| after freeing it.
  // |data| must be nullptr if and only if |size| is 0.
  void reset(void* data, size_t size);

  // Releases ownership of the data owned by this |LpcmPayload|, if any,
  // and returns a pointer to it.
  void* release() FXL_WARN_UNUSED_RESULT;

  void* data_;
  size_t size_;
  std::shared_ptr<Owner> owner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LpcmPayload);
};

}  // namespace media
