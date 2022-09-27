// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/sounds/soundplayer/wav_reader.h"

#include <endian.h>
#include <lib/fdio/fd.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"

namespace soundplayer {
namespace {

static constexpr uint32_t kMinFmtChunkSize = 16;
static constexpr uint16_t kPcmEncoding = 1;
static constexpr uint16_t kPcmFloatEncoding = 3;

}  // namespace

// Change this to FX_LOGS too see details about parse failures in release builds.
#define COMPLAIN() FX_DLOGS(WARNING)

const WavReader::FourCc WavReader::kRiff('R', 'I', 'F', 'F');
const WavReader::FourCc WavReader::kWave('W', 'A', 'V', 'E');
const WavReader::FourCc WavReader::kFmt('f', 'm', 't', ' ');
const WavReader::FourCc WavReader::kData('d', 'a', 't', 'a');

WavReader::WavReader() {}

WavReader::~WavReader() {}

zx_status_t WavReader::Process(DiscardableSound& sound) {
  lseek(sound.fd(), 0, SEEK_SET);

  std::vector<uint8_t> buffer;
  if (!files::ReadFileDescriptorToVector(sound.fd(), &buffer)) {
    COMPLAIN() << "ReadFileDescriptorToVector failed";
    Fail();
    return status_;
  }

  return Process(sound, buffer.data(), buffer.size());
}

zx_status_t WavReader::Process(DiscardableSound& sound, const uint8_t* data, size_t size) {
  buffer_ = data;
  size_ = size;

  status_ = ZX_OK;
  bytes_consumed_ = 0;
  data_writer_ = fit::bind_member(this, &WavReader::WriteDataNoConversion);

  *this >> sound;

  if (!healthy()) {
    COMPLAIN() << "Parse failed";
    return status_;
  }

  if (bytes_remaining() != 0) {
    COMPLAIN() << "Parse did not reach end-of-file";
    Fail();
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

WavReader& WavReader::Fail(zx_status_t status) {
  FX_DCHECK(status != ZX_OK);
  status_ = status;
  return *this;
}

bool WavReader::healthy() const { return status_ == ZX_OK; }

bool WavReader::GetBytes(size_t count, void* dest) {
  if (bytes_remaining() < count) {
    COMPLAIN() << "Reached end-of-file unexpectedly";
    Fail();
    return false;
  }

  if (count == 0) {
    return true;
  }

  FX_DCHECK(dest != nullptr);
  std::memcpy(dest, data(), count);
  bytes_consumed_ += count;

  return true;
}

const void* WavReader::data() {
  FX_CHECK(bytes_consumed_ <= size_);

  return static_cast<const void*>(buffer_ + bytes_consumed_);
}

size_t WavReader::bytes_remaining() const {
  FX_CHECK(bytes_consumed_ <= size_);

  return size_ - bytes_consumed_;
}

bool WavReader::Skip(size_t count) {
  if (bytes_remaining() < count) {
    COMPLAIN() << "Reached end-of-file unexpectedly (skip)";
    Fail();
    return false;
  }

  bytes_consumed_ += count;

  return healthy();
}

WavReader& WavReader::operator>>(DiscardableSound& value) {
  FourCc riff;
  uint32_t file_size;
  FourCc wave;
  *this >> riff >> file_size >> wave;
  if (riff != kRiff) {
    COMPLAIN() << "RIFF tag not found: " << std::hex << riff.value_;  // 52494646
    return Fail();
  }

  if (wave != kWave) {
    COMPLAIN() << "WAVE tag not found";
    return Fail();
  }

  fuchsia::media::AudioStreamType stream_type;
  bool data_read = false;

  while (healthy() && bytes_remaining() != 0) {
    if (bytes_remaining() < 4) {
      // Tolerate up to 3 extra bytes at the end.
      Skip(bytes_remaining());
      break;
    }

    FourCc four_cc;
    *this >> four_cc;

    if (four_cc == kFmt) {
      *this >> stream_type;
    } else if (four_cc == kData) {
      uint32_t chunk_size;
      *this >> chunk_size;
      if (!healthy()) {
        return *this;
      }
      if (chunk_size == 0 || chunk_size > bytes_remaining()) {
        COMPLAIN() << "bad data chunk size " << chunk_size;
        return Fail();
      }

      uint32_t adjusted_size = chunk_size;
      data_writer_(zx::vmo(), nullptr, &adjusted_size);

      zx_status_t status = value.SetSize(adjusted_size);
      if (status != ZX_OK) {
        return Fail(status);
      }

      uint32_t size = chunk_size;
      status = data_writer_(value.LockForWrite(), data(), &size);
      value.Unlock();
      if (status != ZX_OK) {
        return Fail(status);
      }

      Skip(chunk_size);
      data_read = true;
    } else {
      // Ignore unrecognized chunk.
      uint32_t chunk_size;
      *this >> chunk_size;
      Skip(chunk_size);
    }
  }

  if (stream_type.frames_per_second == 0) {
    // No fmt chunk.
    COMPLAIN() << "fmt chunk not found";
    return Fail();
  }

  if (!data_read) {
    // No data chunk.
    COMPLAIN() << "data chunk not found";
    return Fail();
  }

  zx_status_t status = value.SetStreamType(std::move(stream_type));
  if (status != ZX_OK) {
    return Fail(status);
  }

  return *this;
}

WavReader& WavReader::operator>>(FourCc& value) {
  uint32_t value_as_uint32;
  *this >> value_as_uint32;
  value = FourCc(value_as_uint32);
  return *this;
}

WavReader& WavReader::operator>>(uint16_t& value) {
  GetBytes(sizeof(value), &value);
  value = le16toh(value);
  return *this;
}

WavReader& WavReader::operator>>(uint32_t& value) {
  GetBytes(sizeof(value), &value);
  value = le32toh(value);
  return *this;
}

WavReader& WavReader::operator>>(fuchsia::media::AudioStreamType& value) {
  uint32_t chunk_size;
  *this >> chunk_size;
  if (!healthy()) {
    return *this;
  }
  if (chunk_size < kMinFmtChunkSize) {
    COMPLAIN() << "fmt chunk too small";
    return Fail();
  }

  uint16_t encoding;
  *this >> encoding;
  if (!healthy()) {
    return *this;
  }
  if (encoding != kPcmEncoding && encoding != kPcmFloatEncoding) {
    COMPLAIN() << "encoding not recognized: " << encoding;
    return Fail();
  }

  uint16_t channel_count;
  uint32_t byte_rate;        // not used
  uint16_t block_alignment;  // not used
  uint16_t bits_per_sample;

  *this >> channel_count >> value.frames_per_second >> byte_rate >> block_alignment >>
      bits_per_sample;
  if (!healthy()) {
    return *this;
  }
  if (channel_count < 1 || channel_count > 2) {
    COMPLAIN() << "unsupported channel count " << channel_count;
    return Fail();
  }
  if (encoding == kPcmEncoding) {
    switch (bits_per_sample) {
      case 8:
        value.sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
        break;
      case 16:
        value.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
        break;
      case 24:
        value.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
        data_writer_ = fit::bind_member(this, &WavReader::WriteData24To32);
        break;
      case 32:
        value.sample_format = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
        break;
      default:
        COMPLAIN() << "unsupported bits/sample " << bits_per_sample;
        return Fail();
        break;
    }
  } else {
    FX_DCHECK(encoding == kPcmFloatEncoding);
    value.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  }

  value.channels = channel_count;

  Skip(chunk_size - kMinFmtChunkSize);

  return *this;
}

zx_status_t WavReader::WriteDataNoConversion(const zx::vmo& vmo, const void* data,
                                             uint32_t* size_in_out) {
  FX_DCHECK(size_in_out);

  if (!vmo) {
    // The caller wants the size adjusted, which is a no-op.
    return ZX_OK;
  }

  FX_DCHECK(data);

  zx_status_t status = vmo.write(data, 0, *size_in_out);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "zx::vmo::write failed";
  }

  return status;
}

zx_status_t WavReader::WriteData24To32(const zx::vmo& vmo, const void* data,
                                       uint32_t* size_in_out) {
  FX_DCHECK(size_in_out);

  auto size = *size_in_out;

  if (size % 3 != 0) {
    COMPLAIN() << "Data chunk size " << size << " should be a multiple of 3 for 24-bit PCM";
    return ZX_ERR_IO;
  }

  uint32_t sample_count = size / 3;

  if (!vmo) {
    // The caller wants the size adjusted.
    *size_in_out = sample_count * sizeof(int32_t);
    return ZX_OK;
  }

  FX_DCHECK(data);

  auto buffer = std::make_unique<uint8_t[]>(sample_count * sizeof(uint32_t));
  auto from = reinterpret_cast<const uint8_t*>(data);
  auto to = buffer.get();
  for (uint32_t i = 0; i < sample_count; ++i) {
    to[0] = 0;
    to[1] = from[0];
    to[2] = from[1];
    to[3] = from[2];
    to += 4;
    from += 3;
  }

  zx_status_t status = vmo.write(buffer.get(), 0, sample_count * sizeof(int32_t));
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "zx::vmo::write failed";
  }

  *size_in_out = sample_count * sizeof(int32_t);

  return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
// WavReader::FourCc

WavReader::FourCc::FourCc() : value_(0) {}

WavReader::FourCc::FourCc(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    : value_((static_cast<uint64_t>(d) << 24) | (static_cast<uint64_t>(c) << 16) |
             (static_cast<uint64_t>(b) << 8) | static_cast<uint64_t>(a)) {}

WavReader::FourCc::FourCc(uint32_t value) : value_(value) {}

}  // namespace soundplayer
