// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpioutil.h"

int ParseArgs(int argc, char** argv, GpioFunc* func, uint8_t* write_value,
              fuchsia_hardware_gpio::wire::GpioFlags* in_flag, uint8_t* out_value,
              uint64_t* ds_ua) {
  if (argc < 3) {
    return -1;
  }

  *write_value = 0;
  *in_flag = fuchsia_hardware_gpio::wire::GpioFlags::kNoPull;
  *out_value = 0;
  *ds_ua = 0;
  unsigned long flag = 0;
  switch (argv[1][0]) {
    case 'r':
      *func = Read;
      break;
    case 'w':
      *func = Write;

      if (argc < 4) {
        return -1;
      }
      *write_value = static_cast<uint8_t>(std::stoul(argv[3]));
      break;
    case 'i':
      *func = ConfigIn;

      if (argc < 4) {
        return -1;
      }
      flag = std::stoul(argv[3]);
      if (flag > 3) {
        printf("Invalid flag\n\n");
        return -1;
      }
      *in_flag = static_cast<fuchsia_hardware_gpio::wire::GpioFlags>(flag);
      break;
    case 'o':
      *func = ConfigOut;

      if (argc < 4) {
        return -1;
      }
      *out_value = static_cast<uint8_t>(std::stoul(argv[3]));
      break;
    case 'd':
      if (argc >= 4) {
        *func = SetDriveStrength;
        *ds_ua = static_cast<uint64_t>(std::stoull(argv[3]));
      } else if (argc == 3) {
        *func = GetDriveStrength;
      } else {
        return -1;
      }

      break;
    default:
      *func = Invalid;
      return -1;
  }

  return 0;
}

int ClientCall(fidl::WireSyncClient<fuchsia_hardware_gpio::Gpio> client, GpioFunc func,
               uint8_t write_value, fuchsia_hardware_gpio::wire::GpioFlags in_flag,
               uint8_t out_value, uint64_t ds_ua) {
  switch (func) {
    case Read: {
      auto result = client->Read();
      if ((result.status() != ZX_OK) || result.Unwrap_NEW()->is_error()) {
        printf("Could not read GPIO\n");
        return -2;
      }
      printf("GPIO Value: %u\n", result.Unwrap_NEW()->value()->value);
      break;
    }
    case Write: {
      auto result = client->Write(write_value);
      if ((result.status() != ZX_OK) || result.Unwrap_NEW()->is_error()) {
        printf("Could not write to GPIO\n");
        return -2;
      }
      break;
    }
    case ConfigIn: {
      auto result = client->ConfigIn(in_flag);
      if ((result.status() != ZX_OK) || result.Unwrap_NEW()->is_error()) {
        printf("Could not configure GPIO as input\n");
        return -2;
      }
      break;
    }
    case ConfigOut: {
      auto result = client->ConfigOut(out_value);
      if ((result.status() != ZX_OK) || result.Unwrap_NEW()->is_error()) {
        printf("Could not configure GPIO as output\n");
        return -2;
      }
      break;
    }
    case SetDriveStrength: {
      auto result = client->SetDriveStrength(ds_ua);
      if ((result.status() != ZX_OK) || result.Unwrap_NEW()->is_error()) {
        printf("Could not set GPIO drive strength\n");
        return -2;
      }
      printf("Set drive strength to %lu\n", result.Unwrap_NEW()->value()->actual_ds_ua);
      break;
    }
    case GetDriveStrength: {
      auto result = client->GetDriveStrength();
      if ((result.status() != ZX_OK) || result.Unwrap_NEW()->is_error()) {
        printf("Could not get drive strength\n");
        return -2;
      }
      printf("Drive Strength: %lu ua\n", result.Unwrap_NEW()->value()->result_ua);
      break;
    }
    default:
      printf("Invalid function\n\n");
      return -1;
  }
  return 0;
}
