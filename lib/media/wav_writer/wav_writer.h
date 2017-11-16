// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include <stdint.h>
#include <zircon/types.h>

#include "lib/fxl/files/unique_fd.h"

namespace media {
namespace audio {

//
// This class enables a client to easily create and write LPCM audio data to a
// RIFF-based WAV file. After creating the WavWriter object, Initialize should
// be called before invoking other methods. Following that, the Write method
// is used to instruct the library to append the specified number of bytes to
// the audio file that has been created.  Once the client has completely written
// the file, the client should call Close to update 'length' fields in the file
// and close the file.  If the client wishes, it can also occasionally call
// UpdateHeader, to update the 'length' fields prior to file closure.  These
// calls help maximize the amount of audio data retained, in case of a crash
// before file closure, but at the expense of higher file I/O load.
//
// The method Reset discards any previously-written audio data, and returns the
// file to a state of readiness to be provided audio data.
// By contrast, the Delete method removes the file entirely -- following this
// call, the object generally would be destroyed, although it can be reused by
// again calling Initialize.
//
// Note that this library makes no effort to be thread-safe, so the client bears
// all responsibilities for synchronization.
//
template <bool enabled>
class WavWriter {
 public:
  WavWriter() { payload_written_.store(0ul); }
  ~WavWriter() { Close(); }

  void Initialize(const char* const file_name,
                  uint32_t channel_count,
                  uint32_t frame_rate,
                  uint32_t bits_per_sample);

  void Write(void* const buffer, uint32_t num_bytes);
  void UpdateHeader();
  void Reset();
  void Close();
  void Delete();

 private:
  uint32_t channel_count_ = 0;
  uint32_t frame_rate_ = 0;
  uint32_t bits_per_sample_ = 0;

  std::string file_name_;
  fxl::UniqueFD file_;
  size_t payload_written_ = 0;
};

template <>
class WavWriter<false> {
 public:
  void Initialize(const char* const, uint32_t, uint32_t, uint32_t){};
  void Write(void* const, uint32_t){};
  void UpdateHeader(){};
  void Reset(){};
  void Close(){};
  void Delete(){};
};

}  // namespace audio
}  // namespace media
