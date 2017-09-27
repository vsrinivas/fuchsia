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

// Move-only container for packet payloads containing LPCM content.
class LpcmPayload {
 public:
  // Constructs a null |LpcmPayload|.
  LpcmPayload();

  // Constructs a null |LpcmPayload|.
  LpcmPayload(nullptr_t) : LpcmPayload() {}

  // Constructs an |LpcmPayload| from an |LpcmPayload| rvalue.
  LpcmPayload(LpcmPayload&& other);

  // Destroys an |LpcmPayload|, freeing the payload buffer it manages, if any.
  ~LpcmPayload();

  // Returns the sample format of this |LpcmPayload|.
  AudioSampleFormat sample_format() const {
    return owner_ ? owner_->sample_format() : AudioSampleFormat::NONE;
  }

  // Returns the number of channels (samples per frame) for this |LpcmPayload|.
  uint32_t channel_count() const {
    return owner_ ? owner_->channel_count() : 0;
  }

  // Returns the number of bytes per sample for this |LpcmPayload|.
  uint32_t bytes_per_sample() const {
    return owner_ ? owner_->bytes_per_sample() : 0;
  }

  // Returns the number of bytes per frame for this |LpcmPayload|.
  uint32_t bytes_per_frame() const {
    return owner_ ? owner_->channel_count() * owner_->bytes_per_sample() : 0;
  }

  // Returns a pointer to the data owned by this |LpcmPayload|.
  void* data() const { return data_; }

  // Returns a pointer to the data owned by this |LpcmPayload| as a pointer to
  // |SampleType|. In debug builds, |SampleType| is checked to see if it matches
  // the sample format.
  template <typename SampleType>
  SampleType* samples() const {
    FXL_DCHECK(sample_format() == SampleTypeTraits<SampleType>::kSampleFormat);
    return reinterpret_cast<SampleType*>(data_);
  }

  // Returns the size in bytes of the data owned by this |LpcmPayload|.
  size_t size() const { return size_; }

  // Returns the size in frames of the data owned by this |LpcmPayload|.
  size_t frame_count() const { return owner_ ? size_ / bytes_per_frame() : 0; }

  // Returns the size in samples of the data owned by this |LpcmPayload|.
  size_t sample_count() const {
    return owner_ ? size_ / bytes_per_sample() : 0;
  }

  LpcmPayload& operator=(LpcmPayload&& other);

  explicit operator bool() const { return data_ != nullptr; }

  // Frees the payload buffer managed by this |LpcmPayload|.
  void reset();

  // Swaps the contents of this |LpcmPayload| and |other|.
  void swap(LpcmPayload& other);

  // Fills the payload with silence.
  void FillWithSilence();

  // Equality tests with another |LpcmPayload| are nonsensical.
  bool operator==(const LpcmPayload& p2) const = delete;
  bool operator!=(const LpcmPayload& p2) const = delete;

 private:
  friend class LpcmOutputStream;

  // Owner class referenced by |LpcmPayload|s, providing media type information
  // and the ability to release a payload buffer.
  class Owner {
   public:
    virtual ~Owner() {}

    AudioSampleFormat sample_format() const { return sample_format_; }

    uint32_t channel_count() const { return channel_count_; }

    uint32_t bytes_per_sample() const { return bytes_per_sample_; }

    virtual void ReleasePayloadBuffer(void* buffer, size_t size) = 0;

   protected:
    Owner(AudioSampleFormat sample_format, uint32_t channel_count)
        : sample_format_(sample_format),
          channel_count_(channel_count),
          bytes_per_sample_(BytesPerSample(sample_format)) {}

   private:
    AudioSampleFormat sample_format_;
    uint32_t channel_count_;
    uint32_t bytes_per_sample_;
  };

  // Constructs a |LpcmPayload|. |data| must be null if and only if |size| is 0.
  LpcmPayload(void* data, size_t size, std::shared_ptr<Owner> owner);

  // Replaces the data owned by this |LpcmPayload| after freeing it. |data| must
  // be null if and only if |size| is 0.
  void reset(void* data, size_t size, std::shared_ptr<Owner> owner);

  // Releases ownership of the data owned by this |LpcmPayload|, if any,
  // and returns a pointer to it.
  void* release() FXL_WARN_UNUSED_RESULT;

  void* data_;
  size_t size_;
  std::shared_ptr<Owner> owner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LpcmPayload);
};

}  // namespace media
