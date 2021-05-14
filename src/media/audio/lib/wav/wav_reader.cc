// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/wav/wav_reader.h"

#include <endian.h>
#include <fcntl.h>
#include <lib/fdio/io.h>
#include <lib/syslog/cpp/macros.h>
#include <unistd.h>
#include <zircon/compiler.h>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <optional>

#include "src/media/audio/lib/wav/wav_internal.h"

namespace media::audio {

namespace {

// clang-format off
using wav::internal::RIFF_FOUR_CC;
using wav::internal::FMT_FOUR_CC;
using wav::internal::DATA_FOUR_CC;
using wav::internal::WAVE_FOUR_CC;
using wav::internal::RiffChunkHeader;
using wav::internal::WavHeader;
// clang-format on

}  // namespace

// static
fit::result<std::unique_ptr<WavReader>, zx_status_t> WavReader::Open(const std::string& file_name) {
  fbl::unique_fd fd(open(file_name.c_str(), O_RDONLY));
  if (fd.get() < 0) {
    FX_LOGS(WARNING) << "open failed for " << std::quoted(file_name) << ", returned " << fd.get()
                     << ", errno " << errno;
    return fit::error(ZX_ERR_NOT_FOUND);
  }

  // 'RIFF'
  RiffChunkHeader riff_header;
  if (read(fd.get(), &riff_header, sizeof(riff_header)) != sizeof(riff_header)) {
    FX_LOGS(WARNING) << "read initial header failed for " << std::quoted(file_name)
                     << ", amount read was too small; errno " << errno;
    return fit::error(ZX_ERR_IO);
  }
  riff_header.FixupEndianForReading();

  if (riff_header.four_cc != RIFF_FOUR_CC) {
    FX_LOGS(WARNING) << "read initial header failed for " << std::quoted(file_name)
                     << ", unknown RIFF type '"
                     << wav::internal::fourcc_to_string(riff_header.four_cc) << "' (0x" << std::hex
                     << riff_header.four_cc << ") -- expected '"
                     << wav::internal::fourcc_to_string(RIFF_FOUR_CC) << "' (0x" << RIFF_FOUR_CC
                     << ")";
    return fit::error(ZX_ERR_IO);
  }
  if (auto want = sizeof(WavHeader) + sizeof(RiffChunkHeader); riff_header.length < want) {
    FX_LOGS(WARNING) << "RIFF header incorrect for " << std::quoted(file_name)
                     << ", read length of " << riff_header.length << ", expected at least " << want;
    return fit::error(ZX_ERR_IO);
  }
  uint32_t header_size = sizeof(riff_header);
  FX_LOGS(DEBUG) << "Successfully read '" << wav::internal::fourcc_to_string(RIFF_FOUR_CC)
                 << "' header (data length " << riff_header.length << ")";

  // 'WAVE' form_type + 'fmt ' chunk
  WavHeader wav_header;
  if (read(fd.get(), &wav_header, sizeof(wav_header)) != sizeof(wav_header)) {
    FX_LOGS(WARNING) << "read RIFF chunk failed for " << std::quoted(file_name)
                     << ", amount read was too small; errno " << errno;
    return fit::error(ZX_ERR_IO);
  }
  wav_header.FixupEndianForReading();

  if (wav_header.wave_four_cc != WAVE_FOUR_CC) {
    FX_LOGS(WARNING) << "read RIFF form_type failed for " << std::quoted(file_name)
                     << ", unknown type '"
                     << wav::internal::fourcc_to_string(wav_header.wave_four_cc) << "' (0x"
                     << std::hex << wav_header.wave_four_cc << ") -- expected '"
                     << wav::internal::fourcc_to_string(WAVE_FOUR_CC) << "' (0x" << WAVE_FOUR_CC
                     << ")";
    return fit::error(ZX_ERR_IO);
  }
  if (wav_header.fmt_four_cc != FMT_FOUR_CC) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name)
                     << ", unknown chunk '"
                     << wav::internal::fourcc_to_string(wav_header.fmt_four_cc) << "' (0x"
                     << std::hex << wav_header.fmt_four_cc << ") -- expected '"
                     << wav::internal::fourcc_to_string(FMT_FOUR_CC) << "' (0x" << FMT_FOUR_CC
                     << ")";
    return fit::error(ZX_ERR_IO);
  }
  if (wav_header.bits_per_sample != 8 && wav_header.bits_per_sample != 16 &&
      wav_header.bits_per_sample != 24 && wav_header.bits_per_sample != 32) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name)
                     << ", unsupported bits_per_sample: " << wav_header.bits_per_sample;
    return fit::error(ZX_ERR_IO);
  }
  // In the WAV file definition, the format chunk is not constant-size; it specifies its own length.
  // Valid WAV files might have a fmt_chunk_len of 14, 16, 18, 40, etc. (representing valid
  // WAVEFORMAT, PCMWAVEFORMAT, WAVEFORMATEX, WAVEFORMATEXTENSIBLE file types). We can support them
  // all, by reading the essential format info, then skipping the rest of the 'fmt ' chunk.
  auto wav_header_size = offsetof(WavHeader, fmt_chunk_len) + sizeof(wav_header.fmt_chunk_len) +
                         wav_header.fmt_chunk_len;
  header_size += wav_header_size;
  if (wav_header_size != sizeof(WavHeader)) {
    FX_LOGS(INFO) << "'fmt ' chunk is not PCMWAVEFORMAT, adjusting read position by "
                  << static_cast<int>(wav_header_size) - static_cast<int>(sizeof(WavHeader));

    // File read position is at end of 'fmt ' chunkj (we assumed PCMWAVEFORMAT). If fmt_chunk_len
    // is different than that size (could be more or theoretically less), then adjust accordingly.
    // This keeps the file read position in sync with the header_size value.
    off_t pos = lseek(fd.get(), header_size, SEEK_SET);
    if (pos < 0) {
      FX_LOGS(WARNING) << "read RIFF chunk failed for " << std::quoted(file_name)
                       << ", could not seek past the wave header; errno " << errno;
      return fit::error(ZX_ERR_IO);
    }
    FX_CHECK(pos == header_size);
  }
  FX_LOGS(DEBUG) << "Successfully read '" << wav::internal::fourcc_to_string(FMT_FOUR_CC)
                 << "' header (data length " << wav_header.fmt_chunk_len << ")";

  // We find the actual audio samples in a 'data' chunk, usually immediately after the 'fmt ' chunk.
  // Although 'fmt ' and 'data' are the only required chunks in a RIFF-WAV file, optional chunks are
  // fairly common (for metadata like Artist Name, Song Title, etc). By definition, file readers can
  // safely skip any optional chunks, so after the 'fmt ' chunk ends, we skip to the 'data' chunk.
  RiffChunkHeader data_header;
  if (read(fd.get(), &data_header, sizeof(data_header)) != sizeof(data_header)) {
    FX_LOGS(WARNING) << "read data header failed for " << std::quoted(file_name) << ", errno "
                     << errno;
    return fit::error(ZX_ERR_IO);
  }
  data_header.FixupEndianForReading();

  // Keep looping until we find the 'data' chunk
  while (data_header.four_cc != DATA_FOUR_CC) {
    // Skip over this unknown chunk (consisting of a RiffChunkHeader, plus 'length' bytes of data)
    RiffChunkHeader other_header = data_header;
    header_size += sizeof(other_header);
    FX_LOGS(INFO) << "Skipping '" << wav::internal::fourcc_to_string(other_header.four_cc)
                  << "' chunk (data length " << other_header.length << ")";
    header_size += other_header.length;
    lseek(fd.get(), header_size, SEEK_SET);

    // Try again after that chunk: read the next header and fix it up for reading
    if (read(fd.get(), &data_header, sizeof(data_header)) != sizeof(data_header)) {
      // We reached the end of the file before we found a 'DATA' chunk.
      FX_LOGS(WARNING) << "header read (at byte position " << header_size << ") failed for "
                       << std::quoted(file_name) << ", errno " << errno;
      return fit::error(ZX_ERR_IO);
    }
    data_header.FixupEndianForReading();
  }
  FX_LOGS(DEBUG) << "Successfully read '" << wav::internal::fourcc_to_string(DATA_FOUR_CC)
                 << "' header; " << data_header.length << " data bytes follow...";
  header_size += sizeof(data_header);
  FX_LOGS(DEBUG) << "Total header_size for this file: " << header_size << " bytes";

  std::unique_ptr<WavReader> out(new WavReader);
  out->sample_format_ = wav_header.sample_format();
  out->channel_count_ = wav_header.channel_count;
  out->frame_rate_ = wav_header.frame_rate;
  out->bits_per_sample_ = wav_header.bits_per_sample;
  out->length_ = data_header.length;
  out->header_size_ = header_size;
  out->file_ = std::move(fd);

  if (wav_header.bits_per_sample == 24) {
    out->bits_per_sample_ = 32;
    out->length_ = data_header.length * 4 / 3;

    out->packed_24_ = true;
    out->packed_24_buffer_ = std::make_unique<uint8_t[]>(kPacked24BufferSize);
  }

  return fit::ok(std::move(out));
}

fit::result<size_t, int> WavReader::Read(void* buffer, size_t requested_bytes) {
  // In the majority, non-packed-24 case, just read the bytes directly to the client buffer.
  if (!packed_24_) {
    int64_t file_bytes = read(file_.get(), buffer, requested_bytes);

    if (file_bytes < 0) {
      return fit::error(errno);
    }
    return fit::ok(file_bytes);
  }

  // If packed-24, read the file just once, to avoid potential performance problems, then
  // decompress each sample (from 3 to 4 bytes) as we write sequentially into the client buffer.
  int64_t file_bytes_needed = std::min(
      kPacked24BufferSize, (static_cast<int64_t>(requested_bytes) + last_modulo_4_) * 3 / 4);
  int64_t file_bytes = read(file_.get(), packed_24_buffer_.get(), file_bytes_needed);

  if (file_bytes < 0) {
    return fit::error(static_cast<int>(errno));
  }

  auto client_buffer = reinterpret_cast<uint8_t*>(buffer);
  int64_t client_offset = 0, packed_offset = 0;
  while (packed_offset < file_bytes) {
    if ((last_modulo_4_ + client_offset) % 4 == 0) {
      client_buffer[client_offset++] = 0;
    } else {
      client_buffer[client_offset++] = packed_24_buffer_[packed_offset++];
    }
  }
  FX_CHECK(client_offset <= static_cast<int64_t>(requested_bytes));

  last_modulo_4_ = (last_modulo_4_ + client_offset) % 4;
  return fit::ok(static_cast<size_t>(client_offset));
}

int WavReader::Reset() {
  off_t n = lseek(file_.get(), header_size_, SEEK_SET);
  if (n < 0) {
    return static_cast<int>(errno);
  }
  FX_CHECK(n == header_size_);
  return 0;
}

}  // namespace media::audio
