// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <audio-utils/audio-device-stream.h>
#include <audio-utils/audio-input.h>
#include <audio-utils/audio-output.h>
#include <fbl/string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zircon/types.h>
#include <zxtest/zxtest.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

#include "board_name.h"
#include "zircon/errors.h"
#include "zircon/status.h"

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

TEST(IntelHda, DevicePresent) {
  EXPECT_TRUE(audio::intel_hda::IntelHdaDevicePresent(),
              "Expected to find at least one input and one output stream.");
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

std::set<std::string> BoardsWithIntelHda() {
  // List of hardware boards we expect to have Intel HDA hardware.
  //
  // We can't run tests on platforms without the appropriate hardware,
  // and dynamically probing for hardware risks us missing bugs where
  // the driver fails to bring up the hardware correctly. Instead, we
  // have a list of "known supported" platforms.
  return std::set<std::string>({
      "Eve",  // Pixelbook.
  });
}

bool ShouldRunTests(const std::string& board_name) {
  // Run tests if we can see audio inputs/outputs have been populated in the /dev tree.
  if (IntelHdaDevicePresent()) {
    return true;
  }

  // Also run the tests if we know that the hardware we are running on _should_
  // have populated audio inputs/outputs in the /dev tree.
  std::set<std::string> boards = BoardsWithIntelHda();
  return boards.find(board_name) != boards.end();
}

}  // namespace
}  // namespace audio::intel_hda

int main(int argc, char** argv) {
  // Get the hardware platform we are running on.
  fbl::String board_name;
  zx_status_t status = audio::intel_hda::GetBoardName(&board_name);
  if (status != ZX_OK) {
    std::cerr << "Unable to determine hardware platform: " << zx_status_get_string(status) << ".\n";
    return status;
  }
  std::cerr << "Tests running on board '" << board_name.c_str() << "'.\n\n";

  // Only run tests on systems that have (or _should_ have) Intel HDA hardware.
  if (!audio::intel_hda::ShouldRunTests(board_name.c_str())) {
    std::cerr << "No Intel HDA hardware found or expected. Skipping tests.\n";
    return 0;
  }

  // Run tests.
  return RUN_ALL_TESTS(argc, argv);
}
