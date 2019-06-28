// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

#include <iostream>
#include <string>

namespace audio::intel_hda {
namespace {

// Return true if the given file exists.
bool FileExists(const std::string& path) {
  struct stat stat_result;
  return stat(path.c_str(), &stat_result) == 0;
}

// Return true if our system has an Intel HDA device present in the device tree,
// along with at least one supported input codec and output codec.
bool IntelHdaDevicePresent() {
  constexpr char kIntelHdaDefaultDevice[] = "/dev/class/intel-hda/000";
  constexpr char kIntelHdaInputCodecPath[] = "/dev/class/audio-input/000";
  constexpr char kIntelHdaOutputCodecPath[] = "/dev/class/audio-output/000";
  return FileExists(kIntelHdaDefaultDevice) && FileExists(kIntelHdaInputCodecPath) &&
         FileExists(kIntelHdaOutputCodecPath);
}

// Convert a |audio_stream_cmd_get_string_resp_t| into a plain
// std::string_view.
//
// Note: The |response| object should outlive the returned string_view,
// which points into the memory of |response|.
std::string_view StringResponseAsStringView(audio_stream_cmd_get_string_resp_t* response) {
  if (response->strlen > sizeof(response->str)) {
    return "";
  }
  return std::string_view(reinterpret_cast<const char*>(response->str), response->strlen);
}

TEST(IntelHda, BasicStreamInfo) {
  // Open the selected stream.
  constexpr uint32_t kFirstDevice = 0;
  fbl::unique_ptr<audio::utils::AudioDeviceStream> stream =
      audio::utils::AudioOutput::Create(kFirstDevice);
  ASSERT_NE(stream, nullptr);
  zx_status_t status = stream->Open();
  ASSERT_EQ(status, ZX_OK);

  // Fetch manufacturer information, and ensure it is something other than
  // the empty string.
  audio_stream_cmd_get_string_resp_t manufacturer;
  status = stream->GetString(AUDIO_STREAM_STR_ID_MANUFACTURER, &manufacturer);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_GT(StringResponseAsStringView(&manufacturer).length(), 0);

  // Fetch supported audio formats, and ensure it is non-empty.
  fbl::Vector<audio_stream_format_range_t> formats;
  status = stream->GetSupportedFormats(&formats);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_GT(formats.size(), 0);
}

}  // namespace
}  // namespace audio::intel_hda

int main(int argc, char** argv) {
  // If we can't find the Intel HDA device in the /dev tree, skip all
  // associated tests.
  //
  // This isn't ideal, because a failure that prevents discovery of the
  // Intel HDA device will be silently ignored. Longer term, we should
  // determine a better way of determining when to run these tests
  // (board name? PCI device identifier? etc etc).
  if (!audio::intel_hda::IntelHdaDevicePresent()) {
    std::cout << "No Intel HDA device found. Skipping Intel HDA tests.\n";
    return 0;
  }

  return RUN_ALL_TESTS(argc, argv);
}
