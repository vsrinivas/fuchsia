// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/test/comparators.h"
#include "src/media/audio/lib/test/hermetic_golden_test.h"
#include "src/media/audio/lib/test/renderer_shim.h"
#include "src/media/audio/lib/wav/wav_writer.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

bool HermeticPipelineTest::save_input_and_output_files_ = false;

//
// Command line flags set in hermetic_pipeline_test_main.cc.
//

// --save-inputs-and-outputs
// When enabled, save input and output as WAV files for comparison to the golden outputs.
// The saved files are:
//
//    <testname>_input.wav           - the input audio buffer
//    <testname>_ring_buffer.wav     - contents of the entire output ring buffer
//    <testname>_output.wav          - portion of the output ring buffer expected to be non-silent
//    <testname>_expected_output.wav - expected contents of <testname>_output.wav
//
// See
// ./hermetic_golden_test_update_goldens.sh for a semi-automated process.
void set_save_pipeline_test_inputs_and_outputs(bool save_input_and_output_files) {
  HermeticPipelineTest::save_input_and_output_files_ = save_input_and_output_files;
}

template <ASF SampleFormat>
void HermeticPipelineTest::WriteWavFile(const std::string& test_name,
                                        const std::string& file_name_suffix,
                                        AudioBufferSlice<SampleFormat> slice) {
  WavWriter<true> w;
  auto file_name = "/cache/" + test_name + "_" + file_name_suffix + ".wav";
  auto& format = slice.format();
  if (!w.Initialize(file_name.c_str(), format.sample_format(), format.channels(),
                    format.frames_per_second(), format.bytes_per_frame() * 8 / format.channels())) {
    FX_LOGS(ERROR) << "Could not create output file " << file_name;
    return;
  }
  // TODO(fxbug.dev/52161): WavWriter.Write() should take const data
  auto ok =
      w.Write(
          const_cast<typename AudioBufferSlice<SampleFormat>::SampleT*>(&slice.buf()->samples()[0]),
          slice.NumBytes()) &&
      w.UpdateHeader() && w.Close();
  if (!ok) {
    FX_LOGS(ERROR) << "Error writing to output file " << file_name;
  } else {
    FX_LOGS(INFO) << "Wrote output file " << file_name;
  }
}

// Explicitly instantiate all possible implementations.
#define INSTANTIATE(SampleFormat)                                        \
  template void HermeticPipelineTest::WriteWavFile<SampleFormat>(        \
      const std::string& test_name, const std::string& file_name_suffix, \
      AudioBufferSlice<SampleFormat> slice);

INSTANTIATE(ASF::UNSIGNED_8)
INSTANTIATE(ASF::SIGNED_16)
INSTANTIATE(ASF::SIGNED_24_IN_32)
INSTANTIATE(ASF::FLOAT)

}  // namespace media::audio::test
