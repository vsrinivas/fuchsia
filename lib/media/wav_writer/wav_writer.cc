// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/media/wav_writer/wav_writer.h"
#include <endian.h>
#include <fcntl.h>
#include <lib/fdio/io.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <iomanip>
#include <limits>

namespace media {
namespace audio {

namespace {

// Consts for WAV file location, name (instance_count_ is appended), extension
constexpr const char* kDefaultWavFilePathName = "/tmp/wav_writer_";
constexpr const char* kWavFileExtension = ".wav";

//
// Struct and const definitions related to RIFF file format
//

// Encode a 32-bit 'fourcc' value from these 4 byte values
static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

// clang-format off
constexpr uint32_t RIFF_FOUR_CC = make_fourcc('R', 'I', 'F', 'F');
constexpr uint32_t WAVE_FOUR_CC = make_fourcc('W', 'A', 'V', 'E');
constexpr uint32_t FMT_FOUR_CC  = make_fourcc('f', 'm', 't', ' ');
constexpr uint32_t DATA_FOUR_CC = make_fourcc('d', 'a', 't', 'a');
constexpr uint16_t FORMAT_LPCM  = 0x0001;
constexpr uint16_t FORMAT_FLOAT = 0x0003;
// clang-format on

// The RIFF file specification (and the child specification for WAV content)
// defines the layout and contents of WAV audio files.
//
// RIFF files consist of so-called _chunks_ (self-describing sections of the
// file). These files begin with a RIFF header chunk that describes the primary
// format of the file contents, followed by the data itself (in a chunk of its
// own). Additional chunks may also be present, containing metadata and/or other
// information to support optional features. Because all chunks include a length
// field, any unknown chunks can be safely skipped by file readers.
//
// The WAV file format specifies an initial 'RIFF' chunk of type 'WAVE' (length
// 24), followed by two required Subchunks: 'fmt ' (length 24) and 'data'
// (length 8 + the size of the subsequent audio data). Audio data should
// immediately follow these first 8 bytes of the 'data' subchunk. Once the
// entirety of audio data has been written into the file, the 'length' field for
// the 'data' subchunk should be updated with the number of bytes of audio.
// Likewise, the overall length for the parent 'RIFF' chunk (which conceptually
// contains the two 'fmt ' and 'data' subchunks) must be updated at this point,
// to describe its total size (including subchunk headers and the audio data).
// Thus, although all audio data follows the file headers, we must update the
// headers once all audio has been written.
//
// ** Note, lest our RiffChunkHeader struct definition mislead the uninformed **
// These struct definitions actually conceptually relocate the final 32-bit
// value of the initial RIFF chunk into the subsequent 'fmt ' subchunk instead.
// Because the sequence of fields is maintained, this does not create a problem.
// We do this so that we can reuse our RIFF struct definition for the 'data'
// subchunk as well.
struct __PACKED RiffChunkHeader {
  uint32_t four_cc;
  uint32_t length = 0;

  // RIFF files are stored in little-endian, regardless of host-architecture.
  void FixupEndian() {
    four_cc = htole32(four_cc);
    length = htole32(length);
  }
};

// As mentioned above, the WAVE_FOUR_CC is actually a menber of the previous
// RIFF chunk, but we include it here so that we can manage our parent 'RIFF'
// chunk and our 'data' subchunk with common code.
struct __PACKED WavHeader {
  uint32_t wave_four_cc = WAVE_FOUR_CC;
  uint32_t fmt_four_cc = FMT_FOUR_CC;
  uint32_t fmt_chunk_len = sizeof(WavHeader) - offsetof(WavHeader, format);
  uint16_t format = 0;
  uint16_t channel_count = 0;
  uint32_t frame_rate = 0;
  uint32_t average_byte_rate = 0;
  uint16_t frame_size = 0;
  uint16_t bits_per_sample = 0;

  // RIFF files are stored in little-endian, regardless of host-architecture.
  void FixupEndian() {
    // clang-format off
    wave_four_cc      = htole32(wave_four_cc);
    fmt_four_cc       = htole32(fmt_four_cc);
    fmt_chunk_len     = htole32(fmt_chunk_len);
    format            = htole16(format);
    channel_count     = htole16(channel_count);
    frame_rate        = htole32(frame_rate);
    average_byte_rate = htole32(average_byte_rate);
    frame_size        = htole16(frame_size);
    bits_per_sample   = htole16(bits_per_sample);
    // clang-format on
  }
};

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
zx_status_t WriteNewHeader(int file_desc,
                           fuchsia::media::AudioSampleFormat sample_format,
                           uint32_t channel_count, uint32_t frame_rate,
                           uint16_t bits_per_sample) {
  if (channel_count > std::numeric_limits<uint16_t>::max()) {
    return ZX_ERR_INVALID_ARGS;
  }

  ::lseek(file_desc, 0, SEEK_SET);
  RiffChunkHeader riff_header;
  riff_header.four_cc = RIFF_FOUR_CC;
  riff_header.length = kWavHeaderOverhead;
  riff_header.FixupEndian();
  if (::write(file_desc, &riff_header, sizeof(riff_header)) < 0) {
    return ZX_ERR_IO;
  }

  if (sample_format == fuchsia::media::AudioSampleFormat::FLOAT) {
    FXL_DCHECK(bits_per_sample == 32);
  }

  WavHeader wave_header;
  // wave_four_cc already set
  // fmt_four_cc already set
  // fmt_chunk_len already set
  wave_header.format =
      (sample_format == fuchsia::media::AudioSampleFormat::FLOAT) ? FORMAT_FLOAT
                                                                  : FORMAT_LPCM;
  wave_header.channel_count = channel_count;
  wave_header.frame_rate = frame_rate;
  wave_header.average_byte_rate =
      (bits_per_sample >> 3) * channel_count * frame_rate;
  wave_header.frame_size = (bits_per_sample >> 3) * channel_count;
  wave_header.bits_per_sample = bits_per_sample;

  wave_header.FixupEndian();
  if (::write(file_desc, &wave_header, sizeof(wave_header)) < 0) {
    return ZX_ERR_IO;
  }

  riff_header.four_cc = DATA_FOUR_CC;
  riff_header.FixupEndian();
  if (::write(file_desc, &riff_header, sizeof(riff_header)) < 0) {
    return ZX_ERR_IO;
  }

  ::lseek(file_desc, kWavHeaderOverhead, SEEK_SET);
  return ZX_OK;
}

// This function is used to update the 'length' fields in the WAV file header,
// after audio data has been written into the file. Specifically, it updates the
// total length of the 'RIFF' chunk (which includes the headers and all audio
// data), as well as the length of the 'data' subchunk (which includes only the
// audio data). Following this call, the file's write cursor is moved to the end
// of any previously-written audio data, so that subsequent audio writes will be
// correctly appended to the file.
// This private function assumes the given file_desc is valid.
zx_status_t UpdateHeaderLengths(int file_desc, size_t payload_len) {
  if (payload_len >
      (std::numeric_limits<uint32_t>::max() - kWavHeaderOverhead)) {
    return ZX_ERR_INVALID_ARGS;
  }

  uint32_t file_offset = offsetof(RiffChunkHeader, length);
  ::lseek(file_desc, file_offset, SEEK_SET);
  uint32_t new_length =
      htole32(static_cast<uint32_t>(kWavHeaderOverhead + payload_len));
  if (::write(file_desc, &new_length, sizeof(new_length)) < 0) {
    return ZX_ERR_IO;
  }

  file_offset += sizeof(RiffChunkHeader) + sizeof(WavHeader);
  ::lseek(file_desc, file_offset, SEEK_SET);
  new_length = htole32(static_cast<uint32_t>(payload_len));
  if (::write(file_desc, &new_length, sizeof(new_length)) < 0) {
    return ZX_ERR_IO;
  }

  ::lseek(file_desc, kWavHeaderOverhead + payload_len, SEEK_SET);
  return ZX_OK;
}

// This function appends audio data to the WAV file. It assumes that the file's
// write cursor is correctly placed after any previously written audio data.
// This private function assumes the given file_desc is valid.
ssize_t WriteData(int file_desc, const void* const buffer, size_t num_bytes) {
  return ::write(file_desc, buffer, num_bytes);
}

}  // namespace

// Private static ('enabled' specialization) member, for default WAV file name
template <>
fbl::atomic<uint32_t> WavWriter<true>::instance_count_(0u);

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
bool WavWriter<enabled>::Initialize(
    const char* const file_name,
    fuchsia::media::AudioSampleFormat sample_format, uint32_t channel_count,
    uint32_t frame_rate, uint32_t bits_per_sample) {
  // Open our output file.
  uint32_t instance_count = instance_count_.fetch_add(1);
  if (file_name == nullptr || strlen(file_name) == 0) {
    file_name_ = kDefaultWavFilePathName;
    file_name_ += (std::to_string(instance_count) + kWavFileExtension);
  } else {
    file_name_ = file_name;
  }

  int file_desc = ::open(file_name_.c_str(), O_CREAT | O_WRONLY | O_TRUNC);
  file_.reset(file_desc);
  if (!file_.is_valid()) {
    FXL_LOG(WARNING) << "::open failed for " << std::quoted(file_name_)
                     << ", returned " << file_desc << ", errno " << errno;
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
      WriteNewHeader(file_.get(), sample_format_, channel_count_, frame_rate_,
                     bits_per_sample_);
  if (status != ZX_OK) {
    Delete();
    FXL_LOG(WARNING) << "Failed (" << status << ") writing initial header for "
                     << std::quoted(file_name_);
    return false;
  }
  FXL_LOG(INFO) << "WavWriter[" << this << "] recording Format "
                << fidl::ToUnderlying(sample_format_) << ", "
                << bits_per_sample_ << "-bit, " << frame_rate_ << " Hz, "
                << channel_count_ << "-chan PCM to " << std::quoted(file_name_);
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

  ssize_t amt = WriteData(file_.get(), buffer, num_bytes);
  if (amt < 0) {
    FXL_LOG(WARNING) << "Failed (" << amt << ") while writing to "
                     << std::quoted(file_name_);
    return false;
  }

  payload_written_ += amt;
  if (amt < num_bytes) {
    FXL_LOG(WARNING) << "Could not write all bytes to the file. "
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
    FXL_LOG(WARNING) << "Failed (" << status << ") to update WavHeader for "
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
    FXL_LOG(WARNING) << "Invalid file " << std::quoted(file_name_);
    return false;
  }

  payload_written_ = 0;
  if (!UpdateHeader()) {
    return false;
  }

  if (::ftruncate(file_.get(), kWavHeaderOverhead) < 0) {
    FXL_LOG(WARNING) << "Failed to truncate " << std::quoted(file_name_)
                     << ", in WavWriter::Reset().";
    Close();
    return false;
  }

  FXL_LOG(INFO) << "Reset WAV file " << std::quoted(file_name_);
  return true;
}

// Finalize the file (update lengths in headers), and reset our file handle.
// Any subsequent file updates will fail (although Delete can still succeed).
template <bool enabled>
bool WavWriter<enabled>::Close() {
  if (!file_.is_valid()) {
    FXL_LOG(WARNING) << "Invalid file " << std::quoted(file_name_);
    return false;
  }

  // Keep any additional content since the last header update.
  if (!UpdateHeader()) {
    return false;
  }

  file_.reset();
  FXL_LOG(INFO) << "Closed WAV file " << std::quoted(file_name_);
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

  if (::unlink(file_name_.c_str()) < 0) {
    FXL_LOG(WARNING) << "Could not delete " << std::quoted(file_name_);
    return false;
  }

  FXL_LOG(INFO) << "Deleted WAV file " << std::quoted(file_name_);
  return true;
}

// It should always be possible for a client to enable the WavWriter.
template class WavWriter<true>;

}  // namespace audio
}  // namespace media
