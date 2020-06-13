// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gpioutil.h"

void usage(char* prog) {
  fprintf(stderr, "usage:\n");
  fprintf(stderr, "    %s DEVICE r\n", prog);
  fprintf(stderr, "    %s DEVICE w value\n", prog);
  fprintf(stderr, "    %s DEVICE i flags\n", prog);
  fprintf(stderr, "    %s DEVICE o initial_value\n", prog);
}

template <typename T, typename ReturnType>
zx::status<ReturnType> GetStatus(const T& result) {
  if (result.status() != ZX_OK) {
    printf("Unable to connect to device\n");
    return zx::error(result.status());
  }
  if (result->result.has_invalid_tag()) {
    return zx::error(result->result.err());
  }
  return zx::ok(result->result.response().value);
}

template <typename T>
zx::status<> GetStatus(const T& result) {
  if (result.status() != ZX_OK) {
    printf("Unable to connect to device\n");
    return zx::error(result.status());
  }
  if (result->result.has_invalid_tag()) {
    return zx::error(result->result.err());
  }
  return zx::ok();
}

int ParseArgs(int argc, char** argv, GpioFunc* func, uint8_t* write_value,
              ::llcpp::fuchsia::hardware::gpio::GpioFlags* in_flag, uint8_t* out_value) {
  if (argc < 3) {
    usage(argv[0]);
    return -1;
  }

  *write_value = 0;
  *in_flag = ::llcpp::fuchsia::hardware::gpio::GpioFlags::NO_PULL;
  *out_value = 0;
  unsigned long flag = 0;
  switch (argv[2][0]) {
    case 'r':
      *func = Read;
      break;
    case 'w':
      *func = Write;

      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }
      *write_value = static_cast<uint8_t>(std::stoul(argv[3]));
      break;
    case 'i':
      *func = ConfigIn;

      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }
      flag = std::stoul(argv[3]);
      if (flag > 3) {
        usage(argv[0]);
        return -1;
      }
      *in_flag = static_cast<::llcpp::fuchsia::hardware::gpio::GpioFlags>(flag);
      break;
    case 'o':
      *func = ConfigOut;

      if (argc < 4) {
        usage(argv[0]);
        return -1;
      }
      *out_value = static_cast<uint8_t>(std::stoul(argv[3]));
      break;
    default:
      *func = Invalid;
      return -1;
  }

  return 0;
}

int ClientCall(::llcpp::fuchsia::hardware::gpio::Gpio::SyncClient client, GpioFunc func,
               uint8_t write_value, ::llcpp::fuchsia::hardware::gpio::GpioFlags in_flag,
               uint8_t out_value) {
  switch (func) {
    case Read: {
      zx::status<uint8_t> result =
          GetStatus<::llcpp::fuchsia::hardware::gpio::Gpio::ResultOf::Read, uint8_t>(client.Read());
      if (result.is_error()) {
        printf("Could not read GPIO\n");
        return -2;
      }
      printf("GPIO Value: %u\n", result.value());
      break;
    }
    case Write: {
      zx::status<> result = GetStatus(client.Write(write_value));
      if (result.is_error()) {
        printf("Could not write to GPIO\n");
        return -2;
      }
      break;
    }
    case ConfigIn: {
      zx::status<> result = GetStatus(client.ConfigIn(in_flag));
      if (result.is_error()) {
        printf("Could not configure GPIO as input\n");
        return -2;
      }
      break;
    }
    case ConfigOut: {
      zx::status<> result = GetStatus(client.ConfigOut(out_value));
      if (result.is_error()) {
        printf("Could not configure GPIO as output\n");
        return -2;
      }
      break;
    }
    default:
      return -1;
  }
  return 0;
}
