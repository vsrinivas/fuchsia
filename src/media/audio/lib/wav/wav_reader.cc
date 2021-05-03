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
      wav_header.bits_per_sample != 32) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name)
                     << ", unsupported bits_per_sample: " << wav_header.bits_per_sample;
    return fit::error(ZX_ERR_IO);
  }
  header_size += sizeof(wav_header);
  FX_LOGS(DEBUG) << "Successfully read '" << wav::internal::fourcc_to_string(FMT_FOUR_CC)
                 << "' header (data length " << wav_header.fmt_chunk_len << ")";

  //
  // We find the actual audio samples in a 'data' chunk, usually immediately after the 'fmt ' chunk.
  // However, other chunks can precede 'data' (such as a 'LIST' chunk containing a 'info' subchunk
  // with file info). By RIFF 'WAVE' file definition, we can safely skip non-'data' chunks.
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

  return fit::ok(std::move(out));
}

fit::result<size_t, int> WavReader::Read(void* buffer, size_t num_bytes) {
  ssize_t n = read(file_.get(), buffer, num_bytes);
  if (n < 0) {
    return fit::error(static_cast<int>(errno));
  }
  return fit::ok(static_cast<size_t>(n));
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
