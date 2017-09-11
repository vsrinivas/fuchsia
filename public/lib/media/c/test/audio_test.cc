// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include <magenta/syscalls.h>

#include "gtest/gtest.h"

#include "garnet/public/lib/media/c/audio.h"

extern "C" {
fuchsia_audio_manager* audio_manager_create();
int audio_manager_free(fuchsia_audio_manager* manager);
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
                                      mx_duration_t* delay_nsec_out);
int audio_output_stream_write(fuchsia_audio_output_stream* stream,
                              float* sample_buffer,
                              int num_samples,
                              mx_time_t pres_time);
}

namespace media {
namespace client_test {

constexpr char UNKNOWN_DEVICE_STR[] = "unknown_device_id";
constexpr char* UNKNOWN_DEVICE_ID = (char* const)UNKNOWN_DEVICE_STR;
constexpr mx_duration_t TEST_DELAY_VALUE = 0xDEADBEEFABADD00D;
constexpr int WRITE_BUFFER_NUM_SAMPLES = 16 * 3 * 5 * 7;  // Mods w/num_channels

//
// Utility functions
//
void populate_audio_buffer(std::vector<float>* audio_buffer) {
  int buffer_size = audio_buffer->size();
  for (int idx = 0; idx < buffer_size; ++idx) {
    (*audio_buffer)[idx] = static_cast<float>(idx) / 1024;
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

  // Freeing audio_managers out of sequence
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(mgr2));
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(mgr1));
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(mgr4));
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(mgr3));
}

TEST(media_client, audio_manager_errors) {
  size_t local_var;
  fuchsia_audio_manager* manager;

  // Free an unknown audio_manager
  manager = (fuchsia_audio_manager*)&local_var;
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_manager_free(manager));

  // Double-free an audio_manager
  manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_device_descs) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices + 1);

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
  dev_list[num_devices - 1].name[0] = 0;
  dev_list[num_devices - 1].id[0] = 0;
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices + 1));
  ASSERT_EQ(0, dev_list[num_devices].name[0]);
  ASSERT_EQ(0, dev_list[num_devices].id[0]);

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_device_descs_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  // get dev_desc with bad audio_manager
  fuchsia_audio_device_description dev_desc;
  fuchsia_audio_manager* not_an_audio_manager =
      (fuchsia_audio_manager*)&dev_desc;
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_manager_get_output_devices(
                                   not_an_audio_manager, &dev_desc, 1));
  ASSERT_EQ(0, dev_desc.name[0]);

  // get dev_desc with NULL device_description buffer
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_manager_get_output_devices(manager, NULL, 1));

  // get dev_desc with zero num_devices
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 0));
  ASSERT_EQ(0, dev_desc.name[0]);

  // get dev_desc with negative num_devices
  ASSERT_EQ(MX_ERR_OUT_OF_RANGE,
            fuchsia_audio_manager_get_output_devices(manager, &dev_desc, -1));
  ASSERT_EQ(0, dev_desc.name[0]);

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_device_default_params) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_NE(0, param_list[dev_num].sample_rate);
    ASSERT_NE(0, param_list[dev_num].num_channels);
    ASSERT_NE(0, param_list[dev_num].buffer_size);
  }

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_device_default_params_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  // Get device defaults with bad audio_manager
  fuchsia_audio_parameters params;
  fuchsia_audio_manager* not_an_audio_manager = (fuchsia_audio_manager*)&params;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_manager_get_output_device_default_parameters(
                not_an_audio_manager, dev_list[0].id, &params));

  // Get device defaults with NULL device_id
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_manager_get_output_device_default_parameters(
                manager, NULL, &params));

  // Get device defaults with unknown device_id
  ASSERT_EQ(MX_ERR_NOT_FOUND,
            fuchsia_audio_manager_get_output_device_default_parameters(
                // manager, &"mystery_device_id", &params));
                manager, UNKNOWN_DEVICE_ID, &params));

  // Get device defaults with empty device_id
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_manager_get_output_device_default_parameters(
                manager, (char*)"", &params));

  // Get device defaults with NULL params
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_manager_get_output_device_default_parameters(
                manager, dev_list[0].id, NULL));

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
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
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
  }

  // Open all the streams before we start freeing any of them
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_default) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  // Open stream with NULL device_id (default)
  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, NULL, &params, &stream));
  ASSERT_TRUE(stream);
  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));

  // Open stream with empty device_id (default)
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, (char*)"", &params, &stream));
  ASSERT_TRUE(stream);
  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
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
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &params, &stream1));
    ASSERT_TRUE(stream1);

    params.num_channels = 2;
    params.sample_rate = 96000;
    params.buffer_size = 12800;

    fuchsia_audio_output_stream* stream2 = NULL;
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &params, &stream2));
    ASSERT_TRUE(stream2);

    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream1));
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream2));
  }
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  // Open stream with unknown audio_manager
  fuchsia_audio_manager* not_an_audio_manager =
      (fuchsia_audio_manager*)&dev_desc;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_manager_create_output_stream(
                not_an_audio_manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(NULL == stream);

  // Open stream with unknown device_id
  ASSERT_EQ(MX_ERR_NOT_FOUND,
            fuchsia_audio_manager_create_output_stream(
                manager, UNKNOWN_DEVICE_ID, &params, &stream));
  ASSERT_TRUE(NULL == stream);

  // Open stream with NULL stream_out
  ASSERT_EQ(MX_ERR_INVALID_ARGS, fuchsia_audio_manager_create_output_stream(
                                     manager, dev_desc.id, &params, NULL));

  // Free unknown stream
  fuchsia_audio_output_stream* not_an_audio_output_stream =
      (fuchsia_audio_output_stream*)&params;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_output_stream_free(not_an_audio_output_stream));

  // Double-free stream
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(stream);
  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_output_stream_free(stream));

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_params_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  // Open stream with NULL params
  ASSERT_EQ(MX_ERR_INVALID_ARGS, fuchsia_audio_manager_create_output_stream(
                                     manager, dev_desc.id, NULL, &stream));
  ASSERT_TRUE(NULL == stream);

  // Open stream with too-high num_channels
  int saved = params.num_channels;
  params.num_channels = 3;
  ASSERT_EQ(MX_ERR_OUT_OF_RANGE, fuchsia_audio_manager_create_output_stream(
                                     manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(NULL == stream);
  params.num_channels = saved;

  // Open stream with too-low sample_rate
  saved = params.sample_rate;
  params.sample_rate = 7999;
  ASSERT_EQ(MX_ERR_OUT_OF_RANGE, fuchsia_audio_manager_create_output_stream(
                                     manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(NULL == stream);

  // Open stream with too-high sample_rate
  params.sample_rate = 96001;
  ASSERT_EQ(MX_ERR_OUT_OF_RANGE, fuchsia_audio_manager_create_output_stream(
                                     manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(NULL == stream);
  params.sample_rate = saved;

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
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
  std::vector<mx_duration_t> delay_list(num_devices, 0);

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_get_min_delay(
                         stream_list[dev_num], &delay_list[dev_num]));
    ASSERT_TRUE(delay_list[dev_num] > 0);
  }

  // Get all delays before starting to free them
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_delay_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(stream);

  // Get delay with null delay_out
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_output_stream_get_min_delay(stream, NULL));

  // Get delay with unknown stream - delay_nsec should not be touched
  fuchsia_audio_output_stream* not_an_audio_stream;
  mx_duration_t delay_nsec = TEST_DELAY_VALUE;
  not_an_audio_stream = (fuchsia_audio_output_stream*)&delay_nsec;
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_output_stream_get_min_delay(
                                   not_an_audio_stream, &delay_nsec));
  ASSERT_EQ(delay_nsec, TEST_DELAY_VALUE);

  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
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
  std::vector<mx_duration_t> delay_list(num_devices, 0);

  std::vector<float> audio_buffer1(WRITE_BUFFER_NUM_SAMPLES);
  std::vector<float> audio_buffer2(WRITE_BUFFER_NUM_SAMPLES);
  std::vector<float> audio_buffer3(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer1);
  populate_audio_buffer(&audio_buffer2);
  populate_audio_buffer(&audio_buffer3);

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_get_min_delay(
                         stream_list[dev_num], &delay_list[dev_num]));
    ASSERT_TRUE(delay_list[dev_num] > 0);

    mx_time_t pres_time =
        (2 * delay_list[dev_num]) + mx_time_get(MX_CLOCK_MONOTONIC);
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_write(
                         stream_list[dev_num], audio_buffer1.data(),
                         WRITE_BUFFER_NUM_SAMPLES, pres_time));
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_write(
                         stream_list[dev_num], audio_buffer2.data(),
                         param_list[dev_num].num_channels,
                         FUCHSIA_AUDIO_NO_TIMESTAMP));
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_write(
                         stream_list[dev_num], audio_buffer3.data(),
                         param_list[dev_num].num_channels * 32,
                         FUCHSIA_AUDIO_NO_TIMESTAMP));
  }

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_write_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(stream);

  mx_duration_t delay_nsec = 0;
  ASSERT_EQ(MX_OK,
            fuchsia_audio_output_stream_get_min_delay(stream, &delay_nsec));
  ASSERT_TRUE(delay_nsec > 0);

  std::vector<float> audio_buffer(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer);

  // Write with unknown stream
  fuchsia_audio_output_stream* not_an_audio_output_stream =
      (fuchsia_audio_output_stream*)manager;
  mx_time_t pres_time = (delay_nsec * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_output_stream_write(
                not_an_audio_output_stream, audio_buffer.data(),
                WRITE_BUFFER_NUM_SAMPLES, pres_time));

  // Write with NULL audio_buffer
  pres_time = (delay_nsec * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_output_stream_write(
                stream, NULL, WRITE_BUFFER_NUM_SAMPLES, pres_time));

  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_write_samples_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  std::vector<fuchsia_audio_output_stream*> stream_list(num_devices);
  std::vector<mx_duration_t> delay_list(num_devices, 0);

  std::vector<float> audio_buffer(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer);
  mx_time_t pres_time;
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_get_min_delay(
                         stream_list[dev_num], &delay_list[dev_num]));
    ASSERT_TRUE(delay_list[dev_num] > 0);

    // Write mismatch samples (not a multiple of the stream's num_channels)
    if (param_list[dev_num].num_channels > 1) {
      pres_time = (delay_list[dev_num] * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
      ASSERT_EQ(MX_ERR_INVALID_ARGS,
                fuchsia_audio_output_stream_write(
                    stream_list[dev_num], audio_buffer.data(),
                    WRITE_BUFFER_NUM_SAMPLES - 1, pres_time));
    }
  }

  // Write zero samples
  pres_time = (delay_list[0] * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_output_stream_write(
                stream_list[0], audio_buffer.data(), 0, pres_time));

  // Write negative samples
  pres_time = (delay_list[0] * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_INVALID_ARGS, fuchsia_audio_output_stream_write(
                                     stream_list[0], audio_buffer.data(),
                                     -WRITE_BUFFER_NUM_SAMPLES, pres_time));

  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_write_time_errors) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(stream);

  mx_duration_t delay_nsec = 0;
  ASSERT_EQ(MX_OK,
            fuchsia_audio_output_stream_get_min_delay(stream, &delay_nsec));
  ASSERT_TRUE(delay_nsec > 0);

  int buff_size = WRITE_BUFFER_NUM_SAMPLES;
  std::vector<float> audio_buffer(buff_size);
  populate_audio_buffer(&audio_buffer);

  // Write with no pres_time for FIRST buffer
  mx_time_t pres_time = FUCHSIA_AUDIO_NO_TIMESTAMP;
  ASSERT_EQ(MX_ERR_BAD_STATE,
            fuchsia_audio_output_stream_write(stream, audio_buffer.data(),
                                              buff_size, pres_time));

  // Write with zero presentation time
  pres_time = 0;
  ASSERT_EQ(MX_ERR_INVALID_ARGS,
            fuchsia_audio_output_stream_write(stream, audio_buffer.data(),
                                              buff_size, pres_time));

  // Write with invalid presentation time (above INT64_MAX)
  pres_time = FUCHSIA_AUDIO_NO_TIMESTAMP + (delay_nsec * 2) +
              mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_OUT_OF_RANGE,
            fuchsia_audio_output_stream_write(stream, audio_buffer.data(),
                                              buff_size, pres_time));

  // Write with no-delay presentation time
  pres_time = mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_IO_MISSED_DEADLINE,
            fuchsia_audio_output_stream_write(stream, audio_buffer.data(),
                                              buff_size, pres_time));

  // Write with insufficient-delay presentation time
  pres_time = (delay_nsec / 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_IO_MISSED_DEADLINE,
            fuchsia_audio_output_stream_write(stream, audio_buffer.data(),
                                              buff_size, pres_time));

  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_teardown_manager) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = fuchsia_audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  std::vector<fuchsia_audio_device_description> dev_list(num_devices);
  ASSERT_EQ(num_devices, fuchsia_audio_manager_get_output_devices(
                             manager, dev_list.data(), num_devices));

  std::vector<fuchsia_audio_parameters> param_list(num_devices);
  std::vector<fuchsia_audio_output_stream*> stream_list(num_devices);
  std::vector<mx_duration_t> delay_list(num_devices, 0);
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                         manager, dev_list[dev_num].id, &param_list[dev_num]));
    ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                         manager, dev_list[dev_num].id, &param_list[dev_num],
                         &stream_list[dev_num]));
    ASSERT_TRUE(stream_list[dev_num]);
    ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_get_min_delay(
                         stream_list[dev_num], &delay_list[dev_num]));
    ASSERT_TRUE(delay_list[dev_num] > 0);
  }

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));

  // Write after freed audio_manager
  std::vector<float> audio_buffer(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer);
  mx_time_t pres_time = (delay_list[0] * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_output_stream_write(
                                   stream_list[0], audio_buffer.data(),
                                   WRITE_BUFFER_NUM_SAMPLES, pres_time));
  // TODO(mpuryear): On audio_manager free, do active outputs drain their audio?

  // Get min_delay after freed audio_manager
  mx_duration_t delay_nsec = TEST_DELAY_VALUE;
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_output_stream_get_min_delay(
                                   stream_list[0], &delay_nsec));
  ASSERT_EQ(TEST_DELAY_VALUE, delay_nsec);

  // Free stream after freed audio_manager
  for (int dev_num = 0; dev_num < num_devices; ++dev_num) {
    ASSERT_EQ(MX_ERR_BAD_HANDLE,
              fuchsia_audio_output_stream_free(stream_list[dev_num]));
  }

  // Create stream after freed audio_manager
  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_manager_create_output_stream(
                manager, dev_list[0].id, &param_list[0], &stream));
  ASSERT_TRUE(stream == NULL);

  // Get device defaults after freed audio_manager
  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_manager_get_output_device_default_parameters(
                manager, dev_list[0].id, &params));

  // Get device after freed audio_manager
  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  // Get num_devices after freed audio_manager
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_manager_get_output_devices(manager, NULL, 0));
}

TEST(media_client, audio_teardown_streams) {
  fuchsia_audio_manager* manager = fuchsia_audio_manager_create();
  ASSERT_TRUE(manager);

  ASSERT_GE(fuchsia_audio_manager_get_output_devices(manager, NULL, 0), 1)
      << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, fuchsia_audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_OK, fuchsia_audio_manager_create_output_stream(
                       manager, dev_desc.id, &params, &stream));
  ASSERT_TRUE(stream);

  mx_duration_t delay_nsec = 0;
  ASSERT_EQ(MX_OK,
            fuchsia_audio_output_stream_get_min_delay(stream, &delay_nsec));
  ASSERT_TRUE(delay_nsec > 0);

  ASSERT_EQ(MX_OK, fuchsia_audio_output_stream_free(stream));

  // Write after freed audio_stream
  std::vector<float> audio_buffer(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer);
  mx_time_t pres_time = (delay_nsec * 2) + mx_time_get(MX_CLOCK_MONOTONIC);
  ASSERT_EQ(MX_ERR_BAD_HANDLE, fuchsia_audio_output_stream_write(
                                   stream, audio_buffer.data(),
                                   WRITE_BUFFER_NUM_SAMPLES, pres_time));
  // TODO(mpuryear): If an actively playing output is freed, does it drain out?

  // Get delay after freed audio_stream
  delay_nsec = TEST_DELAY_VALUE;
  ASSERT_EQ(MX_ERR_BAD_HANDLE,
            fuchsia_audio_output_stream_get_min_delay(stream, &delay_nsec));
  ASSERT_EQ(delay_nsec, TEST_DELAY_VALUE);

  ASSERT_EQ(MX_OK, fuchsia_audio_manager_free(manager));
}

TEST(media_client, audio_stream_write_c) {
  fuchsia_audio_manager* manager = audio_manager_create();
  ASSERT_TRUE(manager);

  int num_devices = audio_manager_get_output_devices(manager, NULL, 0);
  ASSERT_GE(num_devices, 1) << "** No audio devices found!";

  fuchsia_audio_device_description dev_desc;
  ASSERT_EQ(1, audio_manager_get_output_devices(manager, &dev_desc, 1));

  fuchsia_audio_parameters params;
  ASSERT_EQ(MX_OK, audio_manager_get_output_device_default_parameters(
                       manager, dev_desc.id, &params));

  fuchsia_audio_output_stream* stream = NULL;
  ASSERT_EQ(MX_OK, audio_manager_create_output_stream(manager, dev_desc.id,
                                                      &params, &stream));
  ASSERT_TRUE(stream);

  mx_duration_t delay_nsec = 0;
  ASSERT_EQ(MX_OK, audio_output_stream_get_min_delay(stream, &delay_nsec));
  ASSERT_TRUE(delay_nsec > 0);

  std::vector<float> audio_buffer(WRITE_BUFFER_NUM_SAMPLES);
  populate_audio_buffer(&audio_buffer);
  mx_time_t pres_time = (2 * delay_nsec) + mx_time_get(MX_CLOCK_MONOTONIC);

  ASSERT_EQ(MX_OK,
            audio_output_stream_write(stream, audio_buffer.data(),
                                      WRITE_BUFFER_NUM_SAMPLES, pres_time));

  ASSERT_EQ(MX_OK, audio_output_stream_free(stream));
  ASSERT_EQ(MX_OK, audio_manager_free(manager));
}

}  // namespace client_test
}  // namespace media
