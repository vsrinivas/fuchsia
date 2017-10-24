// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/media/audio_output_manager.h"

#include <cstring>
#include <string>

#include "garnet/lib/media/audio_output_device.h"
#include "garnet/lib/media/audio_output_stream.h"
#include "garnet/lib/media/hw_stub.h"
#include "lib/fxl/logging.h"

namespace media_client {

AudioOutputManager::AudioOutputManager() {
  num_devices_ = kStubNumDevices;
  EnumerateDevices();
}

AudioOutputManager::~AudioOutputManager() {}

void AudioOutputManager::EnumerateDevices() {
  for (int device_num = 0; device_num < num_devices_; ++device_num) {
    std::unique_ptr<AudioOutputDevice> device =
        std::make_unique<AudioOutputDevice>(
            kStubDevId, kStubDevName, kStubPreferredRate,
            kStubPreferredNumChans, kStubPreferredBufferSize);
    devices_.push_back(std::move(device));
  }
}

int AudioOutputManager::GetOutputDevices(
    fuchsia_audio_device_description* buffer,
    int num_device_descriptions) {
  FXL_DCHECK((buffer == nullptr) == (num_device_descriptions == 0));
  FXL_DCHECK(num_device_descriptions >= 0);

  if (!buffer) {
    return num_devices_;
  }

  int dev_num, num_to_copy = std::min(num_device_descriptions, num_devices_);

  for (dev_num = 0; dev_num < num_to_copy; ++dev_num) {
    std::strcpy(buffer[dev_num].id, devices_[dev_num]->id().c_str());
    std::strcpy(buffer[dev_num].name, devices_[dev_num]->name().c_str());
  }
  return num_to_copy;
}

// Returns -1 if there is no device with the given ID.
int AudioOutputManager::GetDeviceNumFromId(char* device_id) {
  // Use |device_id| if present, else use the default device (0)
  if (!device_id || !strlen(device_id)) {
    return (num_devices_ ? 0 : -1);
  }
  for (int dev_num = 0; dev_num < num_devices_; ++dev_num) {
    if (strcmp(devices_[dev_num]->id().c_str(), device_id) == 0) {
      return dev_num;
    }
  }
  return -1;
}

int AudioOutputManager::GetOutputDeviceDefaultParameters(
    char* device_id,
    fuchsia_audio_parameters* stream_params) {
  FXL_DCHECK(stream_params);

  int device_num = GetDeviceNumFromId(device_id);
  if (device_num == -1) {
    return ZX_ERR_NOT_FOUND;  // Device was removed after a previous call to
  }                           // fuchsia_audio_manager_get_output_devices_

  stream_params->sample_rate = devices_[device_num]->preferred_sample_rate();
  stream_params->num_channels = devices_[device_num]->preferred_num_channels();
  stream_params->buffer_size = devices_[device_num]->preferred_buffer_size();

  return ZX_OK;
}

int AudioOutputManager::CreateOutputStream(
    char* device_id,
    fuchsia_audio_parameters* stream_params,
    media_client::AudioOutputStream** stream_out) {
  FXL_DCHECK(stream_params);

  int device_num = GetDeviceNumFromId(device_id);
  if (device_num == -1) {
    return ZX_ERR_NOT_FOUND;  // Device was removed after a previous call to
  }                           // fuchsia_audio_manager_get_output_devices_

  media_client::AudioOutputStream* stream =
      devices_[device_num]->CreateStream(stream_params);
  if (!stream)
    return ZX_ERR_CONNECTION_ABORTED;

  *stream_out = stream;
  return ZX_OK;
}

}  // namespace media_client
