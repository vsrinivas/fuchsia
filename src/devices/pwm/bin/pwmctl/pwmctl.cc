// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pwmctl.h"

#include <stdlib.h>

#include <string>

#include <soc/aml-common/aml-pwm-regs.h>

namespace pwmctl {

constexpr long kBadParse = -1;
long parse_positive_long(const char* number) {
  char* end;
  long result = strtol(number, &end, 10);
  if (end == number || *end != '\0' || result < 0) {
    return kBadParse;
  }
  return result;
}

float parse_positive_float(const char* number) {
  char* end;
  float result = strtof(number, &end);
  if (end == number || *end != '\0' || result < 0) {
    return kBadParse;
  }
  return result;
}

namespace cmd_str {
constexpr char kEnable[] = "enable";
constexpr char kDisable[] = "disable";
constexpr char kSetConfig[] = "config";
}  // namespace cmd_str

zx_status_t enable(fidl::WireSyncClient<fuchsia_hardware_pwm::Pwm>& client) {
  auto result = client.Enable();

  if (!result.ok()) {
    fprintf(stderr, "Failed to enable device\n");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t disable(fidl::WireSyncClient<fuchsia_hardware_pwm::Pwm>& client) {
  auto result = client.Disable();

  if (!result.ok()) {
    fprintf(stderr, "Failed to disable device\n");
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t set_config(fidl::WireSyncClient<fuchsia_hardware_pwm::Pwm>& client, bool polarity,
                       uint32_t period_ns, float duty_cycle) {
  if (duty_cycle < 0.0 || duty_cycle > 100.0) {
    fprintf(stderr, "Duty cycle must be between 0.0 and 100.0\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // TODO(fxbug.dev/41256): This is AML specific, factor this into a plugin or something.
  aml_pwm::mode_config cfg;
  cfg.mode = aml_pwm::ON;

  fuchsia_hardware_pwm::wire::PwmConfig config;
  config.polarity = polarity;
  config.period_ns = period_ns;
  config.duty_cycle = duty_cycle;
  config.mode_config =
      fidl::VectorView<uint8_t>::FromExternal(reinterpret_cast<uint8_t*>(&cfg), sizeof(cfg));

  zx_status_t result = client.SetConfig(config).status();

  if (result != ZX_OK) {
    fprintf(stderr, "Failed to set config, rc = %d\n", result);
  }

  return result;
}

zx_status_t run(int argc, char const* argv[], fidl::ClientEnd<fuchsia_hardware_pwm::Pwm> device) {
  // Pick the command out of the arguments.
  if (argc < 3) {
    fprintf(stderr, "Expected a subcommand\n");
    return ZX_ERR_INVALID_ARGS;
  }

  fidl::WireSyncClient<fuchsia_hardware_pwm::Pwm> client(std::move(device));

  const std::string cmd(argv[2]);
  if (!strncmp(cmd_str::kEnable, argv[2], countof(cmd_str::kEnable))) {
    return enable(client);
  } else if (!strncmp(cmd_str::kDisable, argv[2], countof(cmd_str::kDisable))) {
    return disable(client);
  } else if (!strncmp(cmd_str::kSetConfig, argv[2], countof(cmd_str::kSetConfig))) {
    if (argc < 6) {
      fprintf(stderr, "%s expects 3 arguments %s %s <polarity> <period> <duty_cycle>\n", argv[1],
              argv[0], argv[1]);
      return ZX_ERR_INVALID_ARGS;
    }
    long polarity = parse_positive_long(argv[3]);
    long period_ns_arg = parse_positive_long(argv[4]);
    float duty_cycle = parse_positive_float(argv[5]);

    if (polarity != 1 && polarity != 0) {
      fprintf(stderr, "Polarity must be 0 or 1.\n");
      return ZX_ERR_INVALID_ARGS;
    }

    if (period_ns_arg < 0 || period_ns_arg > std::numeric_limits<uint32_t>::max()) {
      fprintf(stderr, "Invalid argument for period.\n");
      return ZX_ERR_INVALID_ARGS;
    }
    uint32_t period_ns = static_cast<uint32_t>(period_ns_arg);

    if (duty_cycle < 0.0 || duty_cycle > 100.0) {
      fprintf(stderr, "Duty cycle must be between 0.0 and 100.0\n");
      return ZX_ERR_INVALID_ARGS;
    }

    return set_config(client, polarity == 1, period_ns, duty_cycle);
  }

  fprintf(stderr, "Invalid command: %s\n", cmd.c_str());
  return ZX_ERR_INVALID_ARGS;
}

}  // namespace pwmctl
