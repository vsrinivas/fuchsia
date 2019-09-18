// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "audio_test_tools.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/types.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <intel-hda/utils/status_or.h>

namespace audio::intel_hda {

namespace {

// Return true if the given file exists.
bool FileExists(const std::string& path) {
  struct stat stat_result;
  return stat(path.c_str(), &stat_result) == 0;
}

// Return the files in the given directory.
//
// The given path should end with "/".
StatusOr<std::vector<fbl::String>> GetFilesInDir(const char* path) {
  std::vector<fbl::String> result;

  // Open path.
  DIR* dir = opendir(path);
  if (dir == nullptr) {
    return Status(ZX_ERR_INTERNAL,
                  fbl::StringPrintf("Couldn't open directory '%s': %s", path, strerror(errno)));
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
  return result;
}

}  // namespace

SystemAudioDevices GetSystemAudioDevices() {
  SystemAudioDevices results{};
  if (auto inputs = GetFilesInDir("/dev/class/audio-input/"); inputs.ok()) {
    results.inputs = inputs.ValueOrDie();
  }
  if (auto outputs = GetFilesInDir("/dev/class/audio-output/"); outputs.ok()) {
    results.outputs = outputs.ValueOrDie();
  }
  if (auto controllers = GetFilesInDir("/dev/class/intel-hda/"); controllers.ok()) {
    results.controllers = controllers.ValueOrDie();
  }
  return results;
}

bool IsIntelHdaDevicePresent() {
  SystemAudioDevices devices = GetSystemAudioDevices();
  return !devices.controllers.empty() && !devices.inputs.empty() && !devices.outputs.empty();
}

int DeviceNumberFromDevicePath(fbl::String& path) {
  // We assume a format of "/.../123"
  ZX_ASSERT(path.length() > 3);
  return 3;
}

StatusOr<fbl::String> GetStreamConfigString(audio::utils::AudioDeviceStream* stream,
                                            audio_stream_string_id_t id) {
  // Fetch information from stream.
  audio_stream_cmd_get_string_resp_t response;
  zx_status_t status = stream->GetString(id, &response);
  if (status != ZX_OK) {
    return Status(status);
  }

  // Ensure the claimed string length is valid.
  if (response.strlen > sizeof(response.str)) {
    return Status(ZX_ERR_INTERNAL,
                  fbl::StringPrintf("Reponse string length larger than buffer: %d/%ld",
                                    response.strlen, sizeof(response.str)));
  }

  return fbl::String(reinterpret_cast<const char*>(response.str), response.strlen);
}

}  // namespace audio::intel_hda
