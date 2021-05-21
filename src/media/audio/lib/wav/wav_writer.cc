// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/wav/wav_writer.h"

#include <endian.h>
#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <iomanip>
#include <limits>
#include <optional>

#include "src/media/audio/lib/wav/wav_internal.h"

namespace media::audio {

namespace {

// clang-format off
using wav::internal::make_fourcc;
using wav::internal::RIFF_FOUR_CC;
using wav::internal::FMT_FOUR_CC;
using wav::internal::DATA_FOUR_CC;
using wav::internal::RiffChunkHeader;
using wav::internal::WavHeader;
// clang-format on

// Consts for WAV file location, name (instance_count_ is appended), extension
constexpr const char* kDefaultWavFilePathName = "/tmp/wav_writer_";
constexpr const char* kWavFileExtension = ".wav";

//
// Constants
//
// This is the number of bytes from beginning of file, to first audio data byte.
constexpr const uint32_t kWavHeaderOverhead =
    sizeof(RiffChunkHeader) + sizeof(WavHeader) + sizeof(RiffChunkHeader);

//
// Locally-scoped utility functions
//
// This function is used by Initialize to create WAV file headers. Given an
// already-created file, it specifically creates a 'RIFF' chunk of type 'WAVE'
// (length 24) plus its two required Subchunks 'fmt ' (of length 24) and 'data'
// (of length 8 + eventual audio data). Following this call, the file write
// cursor is positioned immediately after the headers, at the correct location
// to write any audio samples we are given.
// This private function assumes the given file_desc is valid.
zx_status_t WriteNewHeader(int file_desc, fuchsia::media::AudioSampleFormat sample_format,
                           uint16_t channel_count, uint32_t frame_rate, uint16_t bits_per_sample) {
  lseek(file_desc, 0, SEEK_SET);
  RiffChunkHeader riff_header;
  riff_header.four_cc = RIFF_FOUR_CC;
  riff_header.length = sizeof(WavHeader) + sizeof(RiffChunkHeader);
  riff_header.FixupEndianForWriting();
  if (write(file_desc, &riff_header, sizeof(riff_header)) < 0) {
    return ZX_ERR_IO;
  }

  if (sample_format == fuchsia::media::AudioSampleFormat::FLOAT) {
    FX_DCHECK(bits_per_sample == 32);
  }

  WavHeader wave_header;
  // wave_four_cc already set
  // fmt_four_cc already set
  // fmt_chunk_len already set
  wave_header.set_format(sample_format);
  wave_header.channel_count = channel_count;
  wave_header.frame_rate = frame_rate;
  wave_header.average_byte_rate = (bits_per_sample >> 3) * channel_count * frame_rate;
  wave_header.frame_size = (bits_per_sample >> 3) * channel_count;
  wave_header.bits_per_sample = bits_per_sample;

  wave_header.FixupEndianForWriting();
  if (write(file_desc, &wave_header, sizeof(wave_header)) < 0) {
    return ZX_ERR_IO;
  }

  riff_header.four_cc = DATA_FOUR_CC;
  riff_header.FixupEndianForWriting();
  if (write(file_desc, &riff_header, sizeof(riff_header)) < 0) {
    return ZX_ERR_IO;
  }

  lseek(file_desc, kWavHeaderOverhead, SEEK_SET);
  return ZX_OK;
}

// This function is used to update the 'length' fields in the WAV file header,
// after audio data has been written into the file. Specifically, it updates the
// total length of the 'RIFF' chunk (which includes the size of rest of the
// headers and all audio data), as well as the length of the 'data' subchunk
// (which includes only the audio data). Following this call, the file's write
// cursor is moved to the end of any previously-written audio data, so that
// subsequent audio writes will be correctly appended to the file.
// This private function assumes the given file_desc is valid.
zx_status_t UpdateHeaderLengths(int file_desc, size_t payload_len) {
  if (payload_len > (std::numeric_limits<uint32_t>::max() - kWavHeaderOverhead)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t file_offset = offsetof(RiffChunkHeader, length);
  lseek(file_desc, file_offset, SEEK_SET);
  auto new_length =
      htole32(static_cast<uint32_t>(sizeof(WavHeader) + sizeof(RiffChunkHeader) + payload_len));
  if (write(file_desc, &new_length, sizeof(new_length)) < 0) {
    return ZX_ERR_IO;
  }

  file_offset += sizeof(RiffChunkHeader) + sizeof(WavHeader);
  lseek(file_desc, file_offset, SEEK_SET);
  new_length = htole32(static_cast<uint32_t>(payload_len));
  if (write(file_desc, &new_length, sizeof(new_length)) < 0) {
    return ZX_ERR_IO;
  }

  lseek(file_desc, kWavHeaderOverhead + payload_len, SEEK_SET);
  return ZX_OK;
}

// This function appends audio data to the WAV file. It assumes that the file's
// write cursor is correctly placed after any previously written audio data.
// This private function assumes the given file_desc is valid.
ssize_t WriteData(int file_desc, const void* const buffer, size_t num_bytes) {
  return write(file_desc, buffer, num_bytes);
}

}  // namespace

// Private static ('enabled' specialization) member, for default WAV file name
template <>
std::atomic<uint32_t> WavWriter<true>::instance_count_(0u);

//
// Public instance methods (general template implementation)
// (Note: the .h contains a 'not enabled' specialization, consisting of no-op
// implementations that are compiler-optimized away. I.e. disabled == zero_cost)
//

// Create the audio file; save the RIFF chunk and 'fmt ' / 'data' sub-chunks.
// If this object already had a file open, the header is not updated.
// TODO(mpuryear): leverage utility code elsewhere for bytes-per-sample lookup,
// for either FIDL-defined sample types and/or driver defined sample packings.
template <bool enabled>
bool WavWriter<enabled>::Initialize(const char* const file_name,
                                    fuchsia::media::AudioSampleFormat sample_format,
                                    uint16_t channel_count, uint32_t frame_rate,
                                    uint16_t bits_per_sample) {
  // Open our output file.
  uint32_t instance_count = instance_count_.fetch_add(1);
  if (file_name == nullptr || strlen(file_name) == 0) {
    file_name_ = kDefaultWavFilePathName;
    file_name_ += (std::to_string(instance_count) + kWavFileExtension);
  } else {
    file_name_ = file_name;
  }

  int file_desc = open(file_name_.c_str(), O_CREAT | O_WRONLY | O_TRUNC);
  file_.reset(file_desc);
  if (!file_.is_valid()) {
    FX_LOGS(WARNING) << "open failed for " << std::quoted(file_name_) << ", returned " << file_desc
                     << ", errno " << errno;
    return false;
  }

  // Save the media format params
  sample_format_ = sample_format;
  channel_count_ = channel_count;
  frame_rate_ = frame_rate;
  bits_per_sample_ = bits_per_sample;
  payload_written_ = 0;

  // Write inital WAV header
  zx_status_t status =
      WriteNewHeader(file_.get(), sample_format_, channel_count_, frame_rate_, bits_per_sample_);
  if (status != ZX_OK) {
    Delete();
    FX_LOGS(WARNING) << "Failed (" << status << ") writing initial header for "
                     << std::quoted(file_name_);
    return false;
  }

  if (bits_per_sample_ == 24) {
    packed_24_buff_ = std::make_unique<uint8_t[]>(kPacked24BufferSize);
  }
  FX_LOGS(INFO) << "WavWriter[" << this << "] recording Format "
                << fidl::ToUnderlying(sample_format_) << ", " << bits_per_sample_ << "-bit, "
                << frame_rate_ << " Hz, " << channel_count_ << "-chan PCM to "
                << std::quoted(file_name_);
  return true;
}

// Write audio data to the file. This assumes that SEEK_SET is at end of file.
// This can be called repeatedly without updating the header's length fields, if
// desired. To update the header, the caller should also invoke UpdateHeader().
template <bool enabled>
bool WavWriter<enabled>::Write(void* const buffer, uint32_t num_bytes) {
  if (!file_.is_valid()) {
    return false;
  }

  auto source_buffer = reinterpret_cast<uint8_t*>(buffer);
  // If bits_per_sample is 24 then write as packed-24 (we've received the audio data as 24-in-32).
  // When compressing each 32-bit sample, we'll skip the first, least-significant of each four
  // bytes. We assume that (file) Write does not buffer, so we copy/compress locally then call Write
  // just once, to avoid potential performance problems.
  if (bits_per_sample_ == 24) {
    FX_CHECK(sample_format_ == fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);

    constexpr auto kPadded24BufferSize = kPacked24BufferSize * 4 / 3;
    while (num_bytes > kPadded24BufferSize) {
      auto succeeded = Write(buffer, kPadded24BufferSize);
      if (!succeeded) {
        return false;
      }
      num_bytes -= kPadded24BufferSize;
      source_buffer += kPadded24BufferSize;
    }

    int64_t write_idx = 0;
    int64_t read_idx = 0;
    while (read_idx < num_bytes) {
      ++read_idx;
      packed_24_buff_[write_idx++] = source_buffer[read_idx++];
      packed_24_buff_[write_idx++] = source_buffer[read_idx++];
      packed_24_buff_[write_idx++] = source_buffer[read_idx++];
    }
    num_bytes = static_cast<uint32_t>(write_idx);
    source_buffer = packed_24_buff_.get();
  }

  ssize_t amt = WriteData(file_.get(), source_buffer, num_bytes);
  if (amt < 0) {
    FX_LOGS(WARNING) << "Failed (" << amt << ") while writing to " << std::quoted(file_name_);
    return false;
  }

  payload_written_ += amt;
  if (amt < num_bytes) {
    FX_LOGS(WARNING) << "Could not write all bytes to the file. "
                     << "Closing file to try to save already-written data.";
    Close();
    return false;
  }

  return true;
}

// We've previously written audio data to the file, so update the length fields.
// This method need not write the entire header -- only the two length fields.
template <bool enabled>
bool WavWriter<enabled>::UpdateHeader() {
  if (!file_.is_valid()) {
    return false;
  }

  zx_status_t status = UpdateHeaderLengths(file_.get(), payload_written_);
  if (status < 0) {
    FX_LOGS(WARNING) << "Failed (" << status << ") to update WavHeader for "
                     << std::quoted(file_name_);
    return false;
  }

  return true;
}

// Discard all previously written audio data, and return the WAV file to an
// empty (but ready to be written) state. Reclaim file space as possible.
template <bool enabled>
bool WavWriter<enabled>::Reset() {
  if (!file_.is_valid()) {
    FX_LOGS(WARNING) << "Invalid file " << std::quoted(file_name_);
    return false;
  }

  payload_written_ = 0;
  if (!UpdateHeader()) {
    return false;
  }

  if (ftruncate(file_.get(), kWavHeaderOverhead) < 0) {
    FX_LOGS(WARNING) << "Failed to truncate " << std::quoted(file_name_)
                     << ", in WavWriter::Reset().";
    Close();
    return false;
  }

  FX_LOGS(INFO) << "Reset WAV file " << std::quoted(file_name_);
  return true;
}

// Finalize the file (update lengths in headers), and reset our file handle.
// Any subsequent file updates will fail (although Delete can still succeed).
template <bool enabled>
bool WavWriter<enabled>::Close() {
  if (!file_.is_valid()) {
    FX_LOGS(WARNING) << "Invalid file " << std::quoted(file_name_);
    return false;
  }

  // Keep any additional content since the last header update.
  if (!UpdateHeader()) {
    return false;
  }

  file_.reset();
  FX_LOGS(INFO) << "Closed WAV file " << std::quoted(file_name_);
  return true;
}

// Eliminate the WAV file (even if we've already closed it).
template <bool enabled>
bool WavWriter<enabled>::Delete() {
  file_.reset();

  // If called before Initialize, do nothing.
  if (file_name_.empty()) {
    return true;
  }

  if (unlink(file_name_.c_str()) < 0) {
    FX_LOGS(WARNING) << "Could not delete " << std::quoted(file_name_);
    return false;
  }

  FX_LOGS(INFO) << "Deleted WAV file " << std::quoted(file_name_);
  return true;
}

// It should always be possible for a client to enable the WavWriter.
template class WavWriter<true>;

}  // namespace media::audio
