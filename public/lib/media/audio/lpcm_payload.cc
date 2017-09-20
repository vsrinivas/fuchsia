// Copyright 2017 The Fuchsia Authors.All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/audio/lpcm_payload.h"

namespace media {

LpcmPayload::LpcmPayload() : data_(nullptr), size_(0), owner_(nullptr) {}

LpcmPayload::LpcmPayload(void* data, size_t size, std::shared_ptr<Owner> owner)
    : data_(data), size_(size), owner_(owner) {
  FXL_DCHECK((data == nullptr) == (size == 0));
  FXL_DCHECK(owner);
}

LpcmPayload::LpcmPayload(LpcmPayload&& other) {
  size_ = other.size_;
  data_ = other.release();
  owner_ = std::move(other.owner_);
}

LpcmPayload::~LpcmPayload() {
  reset();
}

LpcmPayload& LpcmPayload::operator=(LpcmPayload&& other) {
  size_t size = other.size();
  reset(other.release(), size);
  return *this;
}

void LpcmPayload::reset() {
  reset(nullptr, 0);
}

void LpcmPayload::swap(LpcmPayload& other) {
  LpcmPayload temp = std::move(*this);
  *this = std::move(other);
  other = std::move(temp);
}

void LpcmPayload::FillWithSilence() {
  if (data_) {
    std::memset(data_,
                sample_format() == AudioSampleFormat::UNSIGNED_8 ? 0x80 : 0,
                size_);
  }
}

void LpcmPayload::reset(void* data, size_t size) {
  FXL_DCHECK((data == nullptr) == (size == 0));

  if (data_) {
    FXL_DCHECK(owner_);
    owner_->FreePayloadBuffer(data_);
  }

  data_ = data;
  size_ = size;
}

void* LpcmPayload::release() {
  void* data = data_;
  data_ = nullptr;
  size_ = 0;
  return data;
}

}  // namespace media
