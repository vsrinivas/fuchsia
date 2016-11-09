// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/test/fake_wav_reader.h"

#include <mx/datapipe.h>

namespace media {

FakeWavReader::FakeWavReader() : binding_(this) {
  WriteHeader();
}

void FakeWavReader::WriteHeader() {
  header_.clear();

  // Master chunk.
  WriteHeader4CC("RIFF");
  WriteHeaderUint32(size_ - kChunkSizeDeficit);
  WriteHeader4CC("WAVE");  // Format
  FTL_DCHECK(header_.size() == kMasterChunkHeaderSize);

  // Format subchunk.
  WriteHeader4CC("fmt ");
  WriteHeaderUint32(kFormatChunkSize - kChunkSizeDeficit);
  WriteHeaderUint16(kAudioEncoding);
  WriteHeaderUint16(kSamplesPerFrame);
  WriteHeaderUint32(kFramesPerSecond);
  // Byte rate.
  WriteHeaderUint32(kFramesPerSecond * kSamplesPerFrame * kBitsPerSample / 8);
  // Block alignment (frame size in bytes).
  WriteHeaderUint16(kSamplesPerFrame * kBitsPerSample / 8);
  WriteHeaderUint16(kBitsPerSample);
  FTL_DCHECK(header_.size() == kMasterChunkHeaderSize + kFormatChunkSize);

  // Data subchunk.
  WriteHeader4CC("data");
  WriteHeaderUint32(size_ - kMasterChunkHeaderSize - kFormatChunkSize -
                    kChunkSizeDeficit);
  FTL_DCHECK(header_.size() ==
             kMasterChunkHeaderSize + kFormatChunkSize + kDataChunkHeaderSize);
}

FakeWavReader::~FakeWavReader() {}

void FakeWavReader::Bind(fidl::InterfaceRequest<SeekingReader> request) {
  binding_.Bind(std::move(request));
}

void FakeWavReader::Describe(const DescribeCallback& callback) {
  callback(MediaResult::OK, size_, true);
}

void FakeWavReader::ReadAt(uint64_t position, const ReadAtCallback& callback) {
  mx::datapipe_consumer datapipe_consumer;
  mx_status_t status = mx::datapipe<void>::create(
      1u, kDatapipeCapacity, 0u, &datapipe_producer_, &datapipe_consumer);
  FTL_DCHECK(status == NO_ERROR);
  callback(MediaResult::OK, std::move(datapipe_consumer));

  position_ = position;

  WriteToProducer();
}

void FakeWavReader::WriteToProducer() {
  while (true) {
    uint8_t byte = GetByte(position_);
    mx_size_t byte_count;

    mx_status_t status = datapipe_producer_.write(0u, &byte, 1u, &byte_count);
    if (status == NO_ERROR) {
      FTL_DCHECK(byte_count == 1);
      ++position_;
      continue;
    }

    if (status == ERR_SHOULD_WAIT) {
      fidl::GetDefaultAsyncWaiter()->AsyncWait(
          datapipe_producer_.get(), MX_SIGNAL_WRITABLE, MX_TIME_INFINITE,
          FakeWavReader::WriteToProducerStatic, this);
      return;
    }

    // TODO(dalesat): Don't really know what error we're going to get here.
    if (status == ERR_UNAVAILABLE) {
      // Consumer end was closed. This is normal behavior, depending on what
      // the consumer is up to.
      datapipe_producer_.reset();
      return;
    }

    FTL_DCHECK(false) << "mx::datapipe_producer::write failed, status "
                      << status;
  }
}

void FakeWavReader::WriteHeader4CC(const std::string& value) {
  FTL_DCHECK(value.size() == 4);
  header_.push_back(static_cast<uint8_t>(value[0]));
  header_.push_back(static_cast<uint8_t>(value[1]));
  header_.push_back(static_cast<uint8_t>(value[2]));
  header_.push_back(static_cast<uint8_t>(value[3]));
}

void FakeWavReader::WriteHeaderUint16(uint16_t value) {
  header_.push_back(static_cast<uint8_t>(value));
  header_.push_back(static_cast<uint8_t>(value >> 8));
}

void FakeWavReader::WriteHeaderUint32(uint32_t value) {
  header_.push_back(static_cast<uint8_t>(value));
  header_.push_back(static_cast<uint8_t>(value >> 8));
  header_.push_back(static_cast<uint8_t>(value >> 16));
  header_.push_back(static_cast<uint8_t>(value >> 24));
}

uint8_t FakeWavReader::GetByte(size_t position) {
  if (position < header_.size()) {
    // Header.
    return header_[position];
  }

  // Unpleasant sound.
  return static_cast<uint8_t>(position ^ (position >> 8));
}

// static
void FakeWavReader::WriteToProducerStatic(mx_status_t status,
                                          mx_signals_t pending,
                                          void* closure) {
  FakeWavReader* reader = reinterpret_cast<FakeWavReader*>(closure);
  if (status == ERR_BAD_STATE) {
    // Run loop has aborted...the app is shutting down.
    return;
  }

  if (status != NO_ERROR) {
    FTL_LOG(ERROR) << "AsyncWait failed " << status;
    reader->datapipe_producer_.reset();
    return;
  }

  reader->WriteToProducer();
}

}  // namespace media
