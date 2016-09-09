// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/test/fake_wav_reader.h"

#include <mojo/system/result.h>

#include "mojo/public/cpp/system/data_pipe.h"

namespace mojo {
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

void FakeWavReader::Bind(InterfaceRequest<SeekingReader> request) {
  binding_.Bind(request.Pass());
}

void FakeWavReader::Describe(const DescribeCallback& callback) {
  callback.Run(MediaResult::OK, size_, true);
}

void FakeWavReader::ReadAt(uint64_t position, const ReadAtCallback& callback) {
  FTL_DCHECK(!producer_handle_.is_valid())
      << "ReadAt request received with previous datapipe still open";
  ScopedDataPipeConsumerHandle consumer_handle;
  MojoResult result =
      CreateDataPipe(nullptr, &producer_handle_, &consumer_handle);
  FTL_DCHECK(result == MOJO_RESULT_OK);
  FTL_DCHECK(producer_handle_.is_valid());
  FTL_DCHECK(consumer_handle.is_valid());
  callback.Run(MediaResult::OK, consumer_handle.Pass());

  position_ = position;

  WriteToProducerHandle();
}

void FakeWavReader::WriteToProducerHandle() {
  while (true) {
    uint8_t byte = GetByte(position_);
    uint32_t byte_count = 1;

    MojoResult result = WriteDataRaw(producer_handle_.get(), &byte, &byte_count,
                                     MOJO_WRITE_DATA_FLAG_NONE);
    if (result == MOJO_RESULT_OK) {
      FTL_DCHECK(byte_count == 1);
      ++position_;
      continue;
    }

    if (result == MOJO_SYSTEM_RESULT_SHOULD_WAIT) {
      Environment::GetDefaultAsyncWaiter()->AsyncWait(
          producer_handle_.get().value(), MOJO_HANDLE_SIGNAL_WRITABLE,
          MOJO_DEADLINE_INDEFINITE, FakeWavReader::WriteToProducerHandleStatic,
          this);
      return;
    }

    // TODO(dalesat): Remove UNKNOWN when fix lands for
    // https://fuchsia.atlassian.net/projects/US/issues/US-43.
    if (result == MOJO_SYSTEM_RESULT_FAILED_PRECONDITION ||
        result == MOJO_SYSTEM_RESULT_UNKNOWN) {
      // Consumer end was closed. This is normal behavior, depending on what
      // the consumer is up to.
      producer_handle_.reset();
      return;
    }

    FTL_DCHECK(false) << "WriteDataRaw failed, " << result;
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
void FakeWavReader::WriteToProducerHandleStatic(void* reader_void_ptr,
                                                MojoResult result) {
  FakeWavReader* reader = reinterpret_cast<FakeWavReader*>(reader_void_ptr);
  if (result == MOJO_SYSTEM_RESULT_ABORTED) {
    // Run loop has aborted...the app is shutting down.
    return;
  }

  if (result != MOJO_RESULT_OK) {
    MOJO_LOG(ERROR) << "AsyncWait failed " << result;
    reader->producer_handle_.reset();
    return;
  }

  reader->WriteToProducerHandle();
}

}  // namespace media
}  // namespace mojo
