// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_WAV_WAV_WRITER_H_
#define SRC_MEDIA_AUDIO_LIB_WAV_WAV_WRITER_H_

#include <fuchsia/media/cpp/fidl.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>
#include <optional>
#include <string>

#include <fbl/unique_fd.h>

namespace media {
namespace audio {

//
// This class enables a client to easily create and write LPCM audio data to a
// RIFF-based WAV file. After creating the WavWriter object, Initialize should
// be called before invoking other methods. If nullptr or "" is passed to
// Initialize (instead of a valid file name), a default file path+name of
// '/tmp/wav_writer_N.wav' is used, where N is an integer corresponding to the
// instance of WavWriter running in that process.
//
// Following Initialize, the Write method is used to instruct the library to
// append the specified number of bytes to the audio file that has been created.
// Once the client has completely written the file, the client should call Close
// to update 'length' fields in the file and close the file.  If the client
// wishes, it can also occasionally call UpdateHeader, to update the 'length'
// fields prior to file closure.  These calls help maximize the amount of audio
// data retained, in case of a crash before file closure, but at the expense of
// higher file I/O load.
//
// The method Reset discards any previously-written audio data, and returns the
// file to a state of readiness to be provided audio data. By contrast, the
// Delete method removes the file entirely -- subsequently the object would
// generally be destroyed, although it can be revived by re-calling Initialize.
//
// Note that this library makes no effort to be thread-safe, so the client bears
// all responsibilities for synchronization.
//
template <bool enabled = true>
class WavWriter {
 public:
  bool Initialize(const char* const file_name, fuchsia::media::AudioSampleFormat sample_format,
                  uint32_t channel_count, uint32_t frame_rate, uint32_t bits_per_sample);

  bool Write(void* const buffer, uint32_t num_bytes);
  bool UpdateHeader();
  bool Reset();
  bool Close();
  bool Delete();

 private:
  fuchsia::media::AudioSampleFormat sample_format_;
  uint32_t channel_count_ = 0;
  uint32_t frame_rate_ = 0;
  uint32_t bits_per_sample_ = 0;

  std::string file_name_;
  fbl::unique_fd file_;
  size_t payload_written_ = 0;

  static std::atomic<uint32_t> instance_count_;
};

template <>
class WavWriter<false> {
 public:
  bool Initialize(const char* const, fuchsia::media::AudioSampleFormat, uint32_t, uint32_t,
                  uint32_t) {
    return true;
  };
  bool Write(void* const, uint32_t) { return true; };
  bool UpdateHeader() { return true; };
  bool Reset() { return true; };
  bool Close() { return true; };
  bool Delete() { return true; };
};

}  // namespace audio
}  // namespace media

#endif  // SRC_MEDIA_AUDIO_LIB_WAV_WAV_WRITER_H_
