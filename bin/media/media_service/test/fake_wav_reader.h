// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <mx/datapipe.h>

#include "apps/media/services/seeking_reader.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/logging.h"

namespace media {

// Fake SeekingReader that 'reads' a synthetic wav file.
class FakeWavReader : public SeekingReader {
 public:
  // Constructs a FakeWavReader that produces a file of |size| bytes total.
  FakeWavReader();

  ~FakeWavReader() override;

  void SetSize(uint64_t size) {
    FTL_DCHECK(size > kMasterChunkHeaderSize + kFormatChunkSize +
                          kDataChunkHeaderSize);
    size_ = size;
    WriteHeader();
  }

  // Binds the reader.
  void Bind(fidl::InterfaceRequest<SeekingReader> request);

  // SeekingReader implementation.
  void Describe(const DescribeCallback& callback) override;

  void ReadAt(uint64_t position, const ReadAtCallback& callback) override;

 private:
  static constexpr size_t kMasterChunkHeaderSize = 12;
  static constexpr size_t kFormatChunkSize = 24;
  static constexpr size_t kDataChunkHeaderSize = 8;
  static constexpr size_t kChunkSizeDeficit = 8;
  static constexpr mx_size_t kDatapipeCapacity = 65536;

  static constexpr uint64_t kDefaultSize = 64 * 1024;
  static constexpr uint16_t kAudioEncoding = 1;        // PCM
  static constexpr uint16_t kSamplesPerFrame = 2;      // Stereo
  static constexpr uint32_t kFramesPerSecond = 48000;  // 48kHz
  static constexpr uint16_t kBitsPerSample = 16;       // 16-bit samples

  // Callback function for WriteToProducer's async wait.
  static void WriteToProducerStatic(mx_status_t status,
                                    mx_signals_t pending,
                                    void* closure);

  // Writes data to datapipe_producer_ starting at postion_;
  void WriteToProducer();

  // Writes the header to header_.
  void WriteHeader();

  // Writes a 4CC value into header_.
  void WriteHeader4CC(const std::string& value);

  // Writes a uint16 into header_ in little-endian format.
  void WriteHeaderUint16(uint16_t value);

  // Writes a uint32 into header_ in little-endian format.
  void WriteHeaderUint32(uint32_t value);

  // Gets the positionth byte of the file.
  uint8_t GetByte(size_t position);

  fidl::Binding<SeekingReader> binding_;
  std::vector<uint8_t> header_;
  uint64_t size_ = kDefaultSize;
  mx::datapipe_producer datapipe_producer_;
  uint64_t position_;
};

}  // namespace media
