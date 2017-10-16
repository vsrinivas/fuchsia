// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <iostream>
#include <vector>

#include "gtest/gtest.h"

#include "garnet/public/lib/media/c/audio.h"

extern "C" {
fuchsia_audio_manager* audio_manager_create();
void audio_manager_free(fuchsia_audio_manager* manager);
int audio_manager_get_output_devices(
    fuchsia_audio_manager* manager,
    fuchsia_audio_device_description* desc_buff,
    int num_device_descriptions);
int audio_manager_get_output_device_default_parameters(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* params_out);
int audio_manager_create_output_stream(
    fuchsia_audio_manager* manager,
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    fuchsia_audio_output_stream** stream_out);
int audio_output_stream_free(fuchsia_audio_output_stream* stream);
int audio_output_stream_get_min_delay(fuchsia_audio_output_stream* stream,
                                      zx_duration_t* delay_nsec_out);
int audio_output_stream_write(fuchsia_audio_output_stream* stream,
                              float* sample_buffer,
                              int num_samples,
                              zx_time_t pres_time);
}

namespace media {
namespace client_test {

constexpr char UNKNOWN_DEVICE_STR[] = "unknown_device_id";
constexpr char EMPTY_DEVICE_STR[] = "";
constexpr char* UNKNOWN_DEVICE_ID = (char* const)UNKNOWN_DEVICE_STR;
constexpr char* EMPTY_DEVICE_ID = (char* const)EMPTY_DEVICE_STR;
constexpr int WRITE_BUFFER_NUM_SAMPLES = 16 * 3 * 5 * 7;  // Mods w/num_channels

//
// Utility functions
//
void populate_audio_buffer(std::vector<float>* audio_buffer) {
  int buffer_size = audio_buffer->size();
  for (int idx = 0; idx < buffer_size; ++idx) {
    (*audio_buffer)[idx] = 0.0f;
  }
}

//
// Tests
//
TEST(media_client, audio_manager) {
  // create audio_manager
  fuchsia_audio_manager* mgr1 = fuchsia_audio_manager_create();
  ASSERT_TRUE(mgr1);

  // Successive audio_manager allocations are unique
  fuchsia_audio_manager* mgr2 = fuchsia_audio_manager_create();
  ASSERT_TRUE(mgr2);
  ASSERT_NE(mgr1, mgr2);

  fuchsia_audio_manager* mgr3 = fuchsia_audio_manager_create();
  ASSERT_TRUE(mgr3);
  ASSERT_NE(mgr1, mgr3);
  ASSERT_NE(mgr2, mgr3);

  fuchsia_audio_manager* mgr4 = fuchsia_audio_manager_create();
  ASSERT_TRUE(mgr4);
  ASSERT_NE(mgr1, mgr4);
  ASSERT_NE(mgr2, mgr4);
  ASSERT_NE(mgr3, mgr4);

  // TODO(mpuryear): Once the functionality is added to the lib to handle it,
  // add a test to verify the behavior of active streams when the audio_manager
  // object is freed from under them. Does any submitted audio drain out?

  // Freeing audio_managers out of sequence
  fuchsia_audio_manager_free(mgr2);
  fuchsia_audio_manager_free(mgr1);
  fuchsia_audio_manager_free(mgr4);
  fuchsia_audio_manager_free(mgr3);
}

TEST(media_client, audio_device_descs) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices + 1);
  for (fuchsia_audio_device_description dev_desc : dev_list) {
    ::memset(&dev_desc, 0, sizeof(dev_desc));
  }

  if (num_devices > 1) {
    // Get 1 device_description - should not return more than that
    ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager,
                                                          dev_list.data(), 1));
    // Should write into first device_description
    ASSERT_NE(0, dev_list[0].name[0]);
    ASSERT_NE(0, dev_list[0].id[0]);
    // Should not write into subsequent device_description
    ASSERT_EQ(0, dev_list[1].name[0]);
    ASSERT_EQ(0, dev_list[1].id[0]);
  }

  // Get num_devices device_description - should return exactly that many
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));
  // Should write into the final device_description
  ASSERT_NE(0, dev_list[num_devices - 1].name[0]);
  ASSERT_NE(0, dev_list[num_devices - 1].id[0]);
  // Should not write into subsequent device_description
  ASSERT_EQ(0, dev_list[num_devices].name[0]);
  ASSERT_EQ(0, dev_list[num_devices].id[0]);

  // Get (num_devices + 1) device_description - should return only num_devices
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices + 1));
  ASSERT_EQ(0, dev_list[num_devices].name[0]);
  ASSERT_EQ(0, dev_list[num_devices].id[0]);

  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_device_params) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  for (fuchsia_audio_parameters params : param_list) {
    ::memset(&params, 0, sizeof(params));
  }

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_NE(0, param_list[dev_num].sample_rate);
    ASSERT_NE(0, param_list[dev_num].num_channels);
    ASSERT_NE(0, param_list[dev_num].buffer_size);
  }

  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_device_params_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  // Get device defaults with unknown device_id
  fuchsia_audio_parameters params;
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            fuchsia_audio_manager_get_output_device_default_parameters(
                manager, UNKNOWN_DEVICE_ID, &params));

  fuchsia_audio_manager_free(manager);
}

// Verify that nullptr can be passed as device_id.
TEST(media_client, audio_device_default) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  fuchsia_audio_parameters params;
  ::memset(&params, 0, sizeof(params));
  ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, nullptr, &params));
  ASSERT_NE(0, params.sample_rate);
  ASSERT_NE(0, params.num_channels);
  ASSERT_NE(0, params.buffer_size);

  ::memset(&params, 0, sizeof(params));
  ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, EMPTY_DEVICE_ID, &params));
  ASSERT_NE(0, params.sample_rate);
  ASSERT_NE(0, params.num_channels);
  ASSERT_NE(0, params.buffer_size);

  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_streams) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  std::vector<fuchsia_audio_output_stream*> stream_list(num_devices);

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
  }

  // Open all the streams before we start freeing any of them
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }

  // TODO(mpuryear): Once the functionality is added to the lib to handle it,
  // add a test to verify what happens to already-submitted audio when streams
  // are freed. Does the audio drain out?

  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_stream_default) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  // Open stream with NULL device_id (default)
  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, NULL, &params, &stream));
  ASSERT_TRUE(stream);
  ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream));

  // Open stream with empty device_id (default)
  ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, (char*)"", &params, &stream));
  ASSERT_TRUE(stream);
  ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream));

  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_stream_params) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    fuchsia_audio_parameters params;
    params.num_channels = 1;
    params.sample_rate = 8000;
    params.buffer_size = 128;

    fuchsia_audio_output_stream* stream1 = NULL;
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &params, &stream1));
    ASSERT_TRUE(stream1);

    params.num_channels = 2;
    params.sample_rate = 96000;
    params.buffer_size = 12800;

    fuchsia_audio_output_stream* stream2 = NULL;
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &params, &stream2));
    ASSERT_TRUE(stream2);

    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream1));
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream2));
  }
  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_stream_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  // Open stream with unknown device_id
  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(ZX_ERR_NOT_FOUND,
            fuchsia_audio_manager_create_output_stream(
                manager, UNKNOWN_DEVICE_ID, &params, &stream));
  ASSERT_TRUE(NULL == stream);

  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_stream_delay) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  std::vector<fuchsia_audio_output_stream*> stream_list(num_devices);
  std::vector<zx_duration_t> delay_list(num_devices, 0);

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_get_min_delay(
                         stream_list[dev_num], &delay_list[dev_num]));
    ASSERT_TRUE(delay_list[dev_num] > 0);
  }

  // Get all delays before starting to free them
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }
  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_stream_write) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  std::vector<fuchsia_audio_output_stream*> stream_list(num_devices);
  std::vector<zx_duration_t> delay_list(num_devices, 0);

  std::vector<float> audio_buffer1(WRITE_BUFFER_NUM_SAMPLES);
  std::vector<float> audio_buffer2(WRITE_BUFFER_NUM_SAMPLES);
  std::vector<float> audio_buffer3(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer1);
  populate_audio_buffer(&audio_buffer2);
  populate_audio_buffer(&audio_buffer3);

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(ZX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_get_min_delay(
                         stream_list[dev_num], &delay_list[dev_num]));
    ASSERT_TRUE(delay_list[dev_num] > 0);

    zx_time_t pres_time =
        (2 * delay_list[dev_num]) + zx_time_get(ZX_CLOCK_MONOTONIC);
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_write(
                         stream_list[dev_num], audio_buffer1.data(),
                         WRITE_BUFFER_NUM_SAMPLES, pres_time));
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_write(
                         stream_list[dev_num], audio_buffer2.data(),
                         param_list[dev_num].num_channels,
                         FUCHSIA_AUDIO_NO_TIMESTAMP));
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_write(
                         stream_list[dev_num], audio_buffer3.data(),
                         param_list[dev_num].num_channels * 32,
                         FUCHSIA_AUDIO_NO_TIMESTAMP));
  }

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(ZX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }
  fuchsia_audio_manager_free(manager);
}

TEST(media_client, audio_stream_write_c) {
  fuchsia_audio_manager* manager = audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(ZX_OK, audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(ZX_OK, audio_manager_create_output_stream(manager, dev_desc.id,
                                                      &params, &stream));
  ASSERT_TRUE(stream);

  zx_duration_t delay_nsec = 0;
  ASSERT_EQ(ZX_OK, audio_output_stream_get_min_delay(stream, &delay_nsec));
  ASSERT_TRUE(delay_nsec > 0);

  std::vector<float> audio_buffer(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer);
  zx_time_t pres_time = (2 * delay_nsec) + zx_time_get(ZX_CLOCK_MONOTONIC);

  ASSERT_EQ(ZX_OK,
            audio_output_stream_write(stream, audio_buffer.data(),
                                      WRITE_BUFFER_NUM_SAMPLES, pres_time));

  ASSERT_EQ(ZX_OK, audio_output_stream_free(stream));
  audio_manager_free(manager);
}

}  // namespace client_test
}  // namespace media
