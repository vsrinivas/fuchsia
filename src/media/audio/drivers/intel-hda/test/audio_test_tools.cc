// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_test_tools.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>

namespace audio::intel_hda {

namespace {

// Return the files in the given directory.
//
// The given path should end with "/".
zx::result<std::vector<fbl::String>> GetFilesInDir(const char* path) {
  std::vector<fbl::String> result;

  // Open path.
  DIR* dir = opendir(path);
  if (dir == nullptr) {
    std::cerr << "Couldn't open directory " << path << strerror(errno);
    return zx::error(ZX_ERR_INTERNAL);
  }

  // Read through all files.
  while (true) {
    struct dirent* entry = readdir(dir);
    if (entry == nullptr) {
      break;
    }
    result.push_back(fbl::StringPrintf("%s%s", path, entry->d_name));
  }

  closedir(dir);
  return zx::ok(std::move(result));
}

// Create a new stream on the default device.
//
// Return nullptr if there was an error during creation.
template <typename T>
std::unique_ptr<T> CreateAndOpenStream(const char* device) {
  // Create the stream.
  std::unique_ptr<T> stream = T::Create(device);
  if (stream == nullptr) {
    return nullptr;
  }

  // Open the stream.
  zx_status_t status = stream->Open();
  if (status != ZX_OK) {
    std::cerr << "Failed to open audio stream: " << zx_status_get_string(status) << "";
    return nullptr;
  }

  return stream;
}

}  // namespace

SystemAudioDevices GetSystemAudioDevices() {
  SystemAudioDevices results{};
  if (auto inputs = GetFilesInDir("/dev/class/audio-input/"); inputs.is_ok()) {
    results.inputs = inputs.value();
  }
  if (auto outputs = GetFilesInDir("/dev/class/audio-output/"); outputs.is_ok()) {
    results.outputs = outputs.value();
  }
  if (auto controllers = GetFilesInDir("/dev/class/intel-hda/"); controllers.is_ok()) {
    results.controllers = controllers.value();
  }
  return results;
}

bool IsIntelHdaDevicePresent() {
  SystemAudioDevices devices = GetSystemAudioDevices();
  return !devices.controllers.empty() && !devices.inputs.empty() && !devices.outputs.empty();
}

zx::result<fbl::String> GetStreamConfigString(audio::utils::AudioDeviceStream* stream,
                                              audio_stream_string_id_t id) {
  // Fetch information from stream.
  audio_stream_cmd_get_string_resp_t response;
  zx_status_t status = stream->GetString(id, &response);
  if (status != ZX_OK) {
    zx::error(ZX_ERR_INTERNAL);
  }

  // Ensure the claimed string length is valid.
  if (response.strlen > sizeof(response.str)) {
    std::cerr << "Response string length larger than buffer:" << response.strlen
              << sizeof(response.str);
    zx::error(ZX_ERR_INTERNAL);
  }
  fbl::String valid_string =
      fbl::String(reinterpret_cast<const char*>(response.str), response.strlen);
  return zx::ok(valid_string);
}

std::unique_ptr<audio::utils::AudioOutput> CreateAndOpenOutputStream(const char* device) {
  return CreateAndOpenStream<audio::utils::AudioOutput>(device);
}

std::unique_ptr<audio::utils::AudioInput> CreateAndOpenInputStream(const char* device) {
  return CreateAndOpenStream<audio::utils::AudioInput>(device);
}

}  // namespace audio::intel_hda
