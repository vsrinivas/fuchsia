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

  // RIFF.
  RiffChunkHeader riff_header;
  if (read(fd.get(), &riff_header, sizeof(riff_header)) != sizeof(riff_header)) {
    FX_LOGS(WARNING) << "read RIFF header failed for " << std::quoted(file_name) << ", errno "
                     << errno;
    return fit::error(ZX_ERR_IO);
  }

  riff_header.FixupEndianForReading();
  if (riff_header.four_cc != RIFF_FOUR_CC) {
    FX_LOGS(WARNING) << "read RIFF header failed for " << std::quoted(file_name) << ", four_cc 0x"
                     << std::hex << riff_header.four_cc;
    return fit::error(ZX_ERR_IO);
  }
  if (auto want = sizeof(WavHeader) + sizeof(RiffChunkHeader); riff_header.length < want) {
    FX_LOGS(WARNING) << "read RIFF header failed for " << std::quoted(file_name) << ", length "
                     << riff_header.length << ", expected at least " << want;
    return fit::error(ZX_ERR_IO);
  }

  // WAVE + FMT.
  WavHeader wav_header;
  if (read(fd.get(), &wav_header, sizeof(wav_header)) != sizeof(wav_header)) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name) << ", errno "
                     << errno;
    return fit::error(ZX_ERR_IO);
  }

  wav_header.FixupEndianForReading();
  if (wav_header.wave_four_cc != WAVE_FOUR_CC) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name)
                     << ", wave_four_cc 0x" << std::hex << wav_header.wave_four_cc;
    return fit::error(ZX_ERR_IO);
  }
  if (wav_header.fmt_four_cc != FMT_FOUR_CC) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name)
                     << ", fmt_four_cc 0x" << std::hex << wav_header.fmt_four_cc;
    return fit::error(ZX_ERR_IO);
  }
  if (wav_header.bits_per_sample != 8 && wav_header.bits_per_sample != 16 &&
      wav_header.bits_per_sample != 32) {
    FX_LOGS(WARNING) << "read WAV header failed for " << std::quoted(file_name)
                     << ", unsupported bits_per_sample: " << wav_header.bits_per_sample;
    return fit::error(ZX_ERR_IO);
  }

  // DATA.
  RiffChunkHeader data_header;
  if (read(fd.get(), &data_header, sizeof(data_header)) != sizeof(data_header)) {
    FX_LOGS(WARNING) << "read DATA header failed for " << std::quoted(file_name) << ", errno "
                     << errno;
    return fit::error(ZX_ERR_IO);
  }

  data_header.FixupEndianForReading();
  if (data_header.four_cc != DATA_FOUR_CC) {
    FX_LOGS(WARNING) << "read DATA header failed for " << std::quoted(file_name) << ", four_cc 0x"
                     << std::hex << riff_header.four_cc;
    return fit::error(ZX_ERR_IO);
  }

  std::unique_ptr<WavReader> out(new WavReader);
  out->sample_format_ = wav_header.sample_format();
  out->channel_count_ = wav_header.channel_count;
  out->frame_rate_ = wav_header.frame_rate;
  out->bits_per_sample_ = wav_header.bits_per_sample;
  out->length_ = data_header.length;
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

}  // namespace media::audio
