// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/test/fake_wav_reader.h"

#include <mx/socket.h>

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
  if (socket_) {
    if (wait_id_ != 0) {
      fidl::GetDefaultAsyncWaiter()->CancelWait(wait_id_);
      wait_id_ = 0;
    }
    socket_.reset();
  }

  mx::socket other_socket;
  mx_status_t status = mx::socket::create(0u, &socket_, &other_socket);
  FTL_DCHECK(status == MX_OK);
  callback(MediaResult::OK, std::move(other_socket));

  position_ = position;

  WriteToSocket();
}

void FakeWavReader::WriteToSocket() {
  while (true) {
    uint8_t byte = GetByte(position_);
    size_t byte_count;

    mx_status_t status = socket_.write(0u, &byte, 1u, &byte_count);
    if (status == MX_OK) {
      FTL_DCHECK(byte_count == 1);
      ++position_;
      continue;
    }

    if (status == MX_ERR_SHOULD_WAIT) {
      wait_id_ = fidl::GetDefaultAsyncWaiter()->AsyncWait(
          socket_.get(), MX_SOCKET_WRITABLE | MX_SOCKET_PEER_CLOSED,
          MX_TIME_INFINITE, FakeWavReader::WriteToSocketStatic, this);
      return;
    }

    if (status == MX_ERR_PEER_CLOSED) {
      // Consumer end was closed. This is normal behavior, depending on what
      // the consumer is up to.
      socket_.reset();
      return;
    }

    FTL_DCHECK(false) << "mx::socket::write failed, status " << status;
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
void FakeWavReader::WriteToSocketStatic(mx_status_t status,
                                        mx_signals_t pending,
                                        uint64_t count,
                                        void* closure) {
  FakeWavReader* reader = reinterpret_cast<FakeWavReader*>(closure);
  reader->wait_id_ = 0;
  if (status == MX_ERR_CANCELED) {
    // Run loop has aborted...the app is shutting down.
    return;
  }

  if (status != MX_OK) {
    FTL_LOG(ERROR) << "AsyncWait failed " << status;
    reader->socket_.reset();
    return;
  }

  reader->WriteToSocket();
}

}  // namespace media
