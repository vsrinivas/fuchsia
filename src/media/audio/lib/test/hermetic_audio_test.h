// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_TEST_H_
#define SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_TEST_H_

#include <lib/syslog/cpp/macros.h>
#include <zircon/device/audio.h>

#include <unordered_map>
#include <unordered_set>

#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/test/capturer_shim.h"
#include "src/media/audio/lib/test/constants.h"
#include "src/media/audio/lib/test/hermetic_audio_environment.h"
#include "src/media/audio/lib/test/inspect.h"
#include "src/media/audio/lib/test/renderer_shim.h"
#include "src/media/audio/lib/test/test_fixture.h"
#include "src/media/audio/lib/test/virtual_device.h"
#include "zircon/system/ulib/inspect/include/lib/inspect/cpp/hierarchy.h"

namespace media::audio::test {

// Restrictions on usage:
//
// 1. This class is thread hostile: none of its methods can be called concurrently.
// 2. It is illegal for two or more instances to be alive at any time. (This restriction
//    is satisfied by ordinary usage of gtest.)
//
class HermeticAudioTest : public TestFixture {
 protected:
  static void SetUpTestSuite();
  static void SetUpTestSuiteWithOptions(HermeticAudioEnvironment::Options options);
  static void TearDownTestSuite();

  static HermeticAudioEnvironment* environment() {
    auto ptr = HermeticAudioTest::environment_.get();
    FX_CHECK(ptr) << "No Environment; Did you forget to call SetUpTestSuite?";
    return ptr;
  }

  void SetUp() override;
  void TearDown() override;

  // Register that the test expects no audio underflows or overflow.
  // This expectation will be checked by TearDown().
  void FailUponOverflowsOrUnderflows() { disallow_overflows_and_underflows_ = true; }

  // The returned pointers are owned by this class.
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  VirtualOutput<SampleFormat>* CreateOutput(const audio_stream_unique_id_t& device_id,
                                            TypedFormat<SampleFormat> format, size_t frame_count,
                                            DevicePlugProperties* plug_properties = nullptr);

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  VirtualInput<SampleFormat>* CreateInput(const audio_stream_unique_id_t& device_id,
                                          TypedFormat<SampleFormat> format, size_t frame_count,
                                          DevicePlugProperties* plug_properties = nullptr);

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  AudioRendererShim<SampleFormat>* CreateAudioRenderer(
      TypedFormat<SampleFormat> format, size_t frame_count,
      fuchsia::media::AudioRenderUsage usage = fuchsia::media::AudioRenderUsage::MEDIA);

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  AudioCapturerShim<SampleFormat>* CreateAudioCapturer(
      TypedFormat<SampleFormat> format, size_t frame_count,
      fuchsia::media::AudioCapturerConfiguration config);

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  UltrasoundRendererShim<SampleFormat>* CreateUltrasoundRenderer(TypedFormat<SampleFormat> format,
                                                                 size_t frame_count,
                                                                 bool wait_for_creation = true);

  template <fuchsia::media::AudioSampleFormat SampleFormat>
  UltrasoundCapturerShim<SampleFormat>* CreateUltrasoundCapturer(TypedFormat<SampleFormat> format,
                                                                 size_t frame_count,
                                                                 bool wait_for_creation = true);

  // Unbind and forget about the given object.
  void Unbind(VirtualOutputImpl* device);
  void Unbind(VirtualInputImpl* device);
  void Unbind(RendererShimImpl* renderer);
  void Unbind(CapturerShimImpl* capturer);

  // Takes ownership of the AudioDeviceEnumerator. This is useful when tests need to watch for
  // low-level device enumeration events. This is incompatible with CreateInput and CreateOutput.
  fuchsia::media::AudioDeviceEnumeratorPtr TakeOwnershipOfAudioDeviceEnumerator();

  // Direct access to FIDL channels. Using these objects directly may not play well with this class.
  // These are provided for special cases only.
  fuchsia::media::AudioCorePtr audio_core_;
  fuchsia::media::AudioDeviceEnumeratorPtr audio_dev_enum_;

 private:
  static std::unique_ptr<HermeticAudioEnvironment> environment_;
  static fuchsia::virtualaudio::ControlSyncPtr virtual_audio_control_sync_;

  void WatchForDeviceArrivals();
  void WaitForDeviceDepartures();
  void OnDefaultDeviceChanged(uint64_t old_default_token, uint64_t new_default_token);
  void CheckInspectHierarchy(const inspect::Hierarchy& root, const std::vector<std::string>& path,
                             const ExpectedInspectProperties& expected);

  struct DeviceInfo {
    std::unique_ptr<VirtualOutputImpl> output;
    std::unique_ptr<VirtualInputImpl> input;
    std::optional<fuchsia::media::AudioDeviceInfo> info;
    bool is_removed = false;
    bool is_default = false;
  };

  std::unordered_map<uint64_t, std::string> token_to_unique_id_;
  std::unordered_map<std::string, DeviceInfo> devices_;
  std::vector<std::unique_ptr<CapturerShimImpl>> capturers_;
  std::vector<std::unique_ptr<RendererShimImpl>> renderers_;

  fuchsia::ultrasound::FactoryPtr ultrasound_factory_;

  bool disallow_overflows_and_underflows_ = false;
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_LIB_TEST_HERMETIC_AUDIO_TEST_H_
