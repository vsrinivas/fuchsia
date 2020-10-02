// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device-protocol/i2c.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clock.h>
#include <ddk/protocol/codec.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/goldfish/addressspace.h>
#include <ddk/protocol/goldfish/pipe.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/power.h>
#include <ddk/protocol/pwm.h>
#include <ddk/protocol/rpmb.h>
#include <ddk/protocol/spi.h>
#include <ddk/protocol/vreg.h>

#include "../test-metadata.h"

#define DRIVER_NAME "test-composite"
#define GOLDFISH_TEST_HEAP (0x100000000000fffful)

enum Fragments_1 {
  FRAGMENT_PDEV_1, /* Should be 1st fragment */
  FRAGMENT_GPIO_1,
  FRAGMENT_CLOCK_1,
  FRAGMENT_I2C_1,
  FRAGMENT_POWER_1,
  FRAGMENT_CHILD4_1,
  FRAGMENT_CODEC_1,
  FRAGMENT_COUNT_1,
};

enum Fragments_2 {
  FRAGMENT_PDEV_2, /* Should be 1st fragment */
  FRAGMENT_CLOCK_2,
  FRAGMENT_POWER_2,
  FRAGMENT_CHILD4_2,
  FRAGMENT_SPI_2,
  FRAGMENT_PWM_2,
  FRAGMENT_RPMB_2,
  FRAGMENT_VREG_2,
  FRAGMENT_COUNT_2,
};

enum Fragments_GoldfishControl {
  FRAGMENT_PDEV_GOLDFISH_CTRL, /* Should be 1st fragment */
  FRAGMENT_GOLDFISH_ADDRESS_SPACE_GOLDFISH_CTRL,
  FRAGMENT_GOLDFISH_PIPE_GOLDFISH_CTRL,
  FRAGMENT_COUNT_GOLDFISH_CTRL,
};

typedef struct {
  zx_device_t* zxdev;
} test_t;

typedef struct {
  uint32_t magic;
} mode_config_magic_t;

typedef struct {
  uint32_t mode;
  union {
    mode_config_magic_t magic;
  };
} mode_config_t;

static void test_release(void* ctx) { free(ctx); }

static zx_protocol_device_t test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = test_release,
};

static zx_status_t check_handle_type(zx_handle_t handle, zx_obj_type_t type) {
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_info_handle_basic_t handle_info;

  zx_status_t status = zx_object_get_info(handle, ZX_INFO_HANDLE_BASIC, &handle_info,
                                          sizeof(handle_info), NULL, NULL);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: zx_object_get_info (handle = %u) failed: %d", __func__, handle, status);
    return status;
  }

  if (handle_info.type != type) {
    zxlogf(ERROR, "%s: type of handle %u (%u) doesn't match target type %u", __func__, handle,
           handle_info.type, type);
    return ZX_ERR_WRONG_TYPE;
  }
  return ZX_OK;
}

static zx_status_t test_goldfish_address_space(goldfish_address_space_protocol_t* addr_space) {
  zx_status_t status;

  zx_handle_t client, server;
  if ((status = zx_channel_create(0u, &client, &server)) != ZX_OK) {
    zxlogf(ERROR, "%s: zx_channel_create failed: %d", DRIVER_NAME, status);
    return status;
  }

  if ((status = goldfish_address_space_open_child_driver(
           addr_space, ADDRESS_SPACE_CHILD_DRIVER_TYPE_DEFAULT, server)) != ZX_OK) {
    zxlogf(ERROR, "%s: goldfish_address_space_open_child_driver failed: %d", DRIVER_NAME, status);
    return status;
  }

  return ZX_OK;
}

static zx_status_t test_goldfish_pipe(goldfish_pipe_protocol_t* pipe) {
  zx_status_t status;

  zx_handle_t event;
  if ((status = zx_event_create(0u, &event)) != ZX_OK) {
    zxlogf(ERROR, "%s: zx_event_create failed: %d", DRIVER_NAME, status);
    return status;
  }

  zx_handle_t sysmem_client, sysmem_server;
  if ((status = zx_channel_create(0u, &sysmem_client, &sysmem_server)) != ZX_OK) {
    zxlogf(ERROR, "%s: zx_channel_create failed: %d", DRIVER_NAME, status);
    return status;
  }

  zx_handle_t heap_client, heap_server;
  if ((status = zx_channel_create(0u, &heap_client, &heap_server)) != ZX_OK) {
    zxlogf(ERROR, "%s: zx_channel_create failed: %d", DRIVER_NAME, status);
    return status;
  }

  int32_t id;
  zx_handle_t vmo;

  // Test |GoldfishPipe.Create|.
  if ((status = goldfish_pipe_create(pipe, &id, &vmo)) != ZX_OK) {
    zxlogf(ERROR, "%s: goldfish_pipe_create failed: %d", DRIVER_NAME, status);
    return status;
  }

  // Check if |vmo| is valid.
  if ((status = check_handle_type(vmo, ZX_OBJ_TYPE_VMO)) != ZX_OK) {
    zxlogf(ERROR, "%s: vmo handle/type invalid: %d", DRIVER_NAME, status);
    return status;
  }

  // Test |GoldfishPipe.SetEvent|.
  if ((status = goldfish_pipe_set_event(pipe, id, event)) != ZX_OK) {
    zxlogf(ERROR, "%s: goldfish_pipe_set_event failed: %d", DRIVER_NAME, status);
    return status;
  }

  // Test |GoldfishPipe.Open|.
  goldfish_pipe_open(pipe, id);

  // Test |GoldfishPipe.Exec|.
  goldfish_pipe_exec(pipe, id);

  // Test |GoldfishPipe.GetBti|.
  zx_handle_t bti;
  if ((status = goldfish_pipe_get_bti(pipe, &bti)) != ZX_OK) {
    zxlogf(ERROR, "%s: goldfish_pipe_get_bti failed: %d", DRIVER_NAME, status);
    return status;
  }

  // Test |GoldfishPipe.ConnectSysmem|.
  if ((status = goldfish_pipe_connect_sysmem(pipe, sysmem_server)) != ZX_OK) {
    zxlogf(ERROR, "%s: goldfish_pipe_connect_sysmem failed: %d", DRIVER_NAME, status);
    return status;
  }

  // Test |GoldfishPipe.RegisterSysmemHeap|.
  if ((status = goldfish_pipe_register_sysmem_heap(pipe, GOLDFISH_TEST_HEAP, heap_server)) !=
      ZX_OK) {
    zxlogf(ERROR, "%s: goldfish_pipe_register_sysmem_heap failed: %d", DRIVER_NAME, status);
    return status;
  }

  // Test |GoldfishPipe.Destroy|.
  goldfish_pipe_destroy(pipe, id);

  return ZX_OK;
}

static zx_status_t test_gpio(gpio_protocol_t* gpio) {
  zx_status_t status;
  uint8_t value;

  if ((status = gpio_config_out(gpio, 0)) != ZX_OK) {
    return status;
  }
  if ((status = gpio_read(gpio, &value)) != ZX_OK || value != 0) {
    return status;
  }
  if ((status = gpio_write(gpio, 1)) != ZX_OK) {
    return status;
  }
  if ((status = gpio_read(gpio, &value)) != ZX_OK || value != 1) {
    return status;
  }

  return ZX_OK;
}

static zx_status_t test_clock(clock_protocol_t* clock) {
  zx_status_t status;
  const uint64_t kOneMegahertz = 1000000;
  const uint32_t kBad = 0xDEADBEEF;
  uint64_t out_rate = 0;

  if ((status = clock_enable(clock)) != ZX_OK) {
    return status;
  }
  if ((status = clock_disable(clock)) != ZX_OK) {
    return status;
  }

  bool is_enabled = false;
  if ((status = clock_is_enabled(clock, &is_enabled)) != ZX_OK) {
    return status;
  }

  if ((status = clock_set_rate(clock, kOneMegahertz)) != ZX_OK) {
    return status;
  }

  if ((status = clock_query_supported_rate(clock, kOneMegahertz, &out_rate)) != ZX_OK) {
    return status;
  }

  if ((status = clock_get_rate(clock, &out_rate)) != ZX_OK) {
    return status;
  }

  if ((status = clock_set_input(clock, 0)) != ZX_OK) {
    return status;
  }

  uint32_t num_inputs = kBad;
  if ((status = clock_get_num_inputs(clock, &num_inputs)) != ZX_OK) {
    return status;
  }

  uint32_t current_input = kBad;
  if ((status = clock_get_input(clock, &current_input)) != ZX_OK) {
    return status;
  }

  // Make sure that the input value was actually set.
  if (num_inputs == kBad || current_input == kBad) {
    // The above calls returned ZX_OK but the out value was unchanged?
    return ZX_ERR_BAD_STATE;
  }

  return ZX_OK;
}

static zx_status_t test_i2c(i2c_protocol_t* i2c) {
  size_t max_transfer;

  // i2c test driver returns 1024 for max transfer size
  zx_status_t status = i2c_get_max_transfer_size(i2c, &max_transfer);
  if (status != ZX_OK || max_transfer != 1024) {
    zxlogf(ERROR, "%s: i2c_get_max_transfer_size failed", DRIVER_NAME);
    return ZX_ERR_INTERNAL;
  }

  // i2c test driver reverses digits
  const uint32_t write_digits[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  uint32_t read_digits[10];
  memset(read_digits, 0, sizeof(read_digits));

  status = i2c_write_read_sync(i2c, write_digits, sizeof(write_digits), read_digits,
                               sizeof(read_digits));
  if (status != ZX_OK || max_transfer != 1024) {
    zxlogf(ERROR, "%s: i2c_write_read_sync failed %d", DRIVER_NAME, status);
    return status;
  }

  for (size_t i = 0; i < countof(read_digits); i++) {
    if (read_digits[i] != write_digits[countof(read_digits) - i - 1]) {
      zxlogf(ERROR, "%s: read_digits does not match reverse of write digits", DRIVER_NAME);
      return ZX_ERR_INTERNAL;
    }
  }

  return ZX_OK;
}

static zx_status_t test_spi(spi_protocol_t* spi) {
  const uint8_t txbuf[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  uint8_t rxbuf[sizeof txbuf];

  // tx should just succeed
  zx_status_t status = spi_transmit(spi, txbuf, sizeof txbuf);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: spi_transmit failed %d", DRIVER_NAME, status);
    return status;
  }

  // rx should return pattern
  size_t actual;
  memset(rxbuf, 0, sizeof rxbuf);
  status = spi_receive(spi, sizeof rxbuf, rxbuf, sizeof rxbuf, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: spi_receive failed %d (", DRIVER_NAME, status);
    return status;
  }

  if (actual != sizeof rxbuf) {
    zxlogf(ERROR, "%s: spi_receive returned incomplete %zu/%zu (", DRIVER_NAME, actual,
           sizeof rxbuf);
    return ZX_ERR_INTERNAL;
  }

  for (size_t i = 0; i < actual; i++) {
    if (rxbuf[i] != (i & 0xff)) {
      zxlogf(ERROR, "%s: spi_receive returned bad pattern rxbuf[%zu] = 0x%02x, should be 0x%02x(",
             DRIVER_NAME, i, rxbuf[i], (uint8_t)(i & 0xff));
      return ZX_ERR_INTERNAL;
    }
  }

  // exchange copies input
  memset(rxbuf, 0, sizeof rxbuf);
  status = spi_exchange(spi, txbuf, sizeof txbuf, rxbuf, sizeof rxbuf, &actual);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: spi_exchange failed %d (", DRIVER_NAME, status);
    return status;
  }

  if (actual != sizeof rxbuf) {
    zxlogf(ERROR, "%s: spi_exchange returned incomplete %zu/%zu (", DRIVER_NAME, actual,
           sizeof rxbuf);
    return ZX_ERR_INTERNAL;
  }

  for (size_t i = 0; i < actual; i++) {
    if (rxbuf[i] != txbuf[i]) {
      zxlogf(ERROR, "%s: spi_exchange returned bad result rxbuf[%zu] = 0x%02x, should be 0x%02x(",
             DRIVER_NAME, i, rxbuf[i], txbuf[i]);
      return ZX_ERR_INTERNAL;
    }
  }

  return ZX_OK;
}

static zx_status_t test_power(power_protocol_t* power) {
  zx_status_t status;
  uint32_t value;

  uint32_t min_voltage = 0, max_voltage = 0;
  if ((status = power_get_supported_voltage_range(power, &min_voltage, &max_voltage)) != ZX_OK) {
    // Not a fixed power domain.
    zxlogf(ERROR, "Unable to get supported voltage from power domain");
    return status;
  }

  // These are the limits in the test power-impl driver
  if (min_voltage != 10 && max_voltage != 1000) {
    zxlogf(ERROR, "%s: Got wrong supported voltages", __func__);
    return ZX_ERR_INTERNAL;
  }

  if ((status = power_register_power_domain(power, 50, 800)) != ZX_OK) {
    zxlogf(ERROR, "Unable to register for power domain");
    return status;
  }

  power_domain_status_t out_status;
  if ((status = power_get_power_domain_status(power, &out_status) != ZX_OK)) {
    zxlogf(ERROR, "Unable to power domain status");
    return status;
  }

  if (out_status != POWER_DOMAIN_STATUS_ENABLED) {
    zxlogf(ERROR, "power domain should have been enabled after registration");
    return ZX_ERR_INTERNAL;
  }

  uint32_t out_actual_voltage = 0;
  if ((status = power_request_voltage(power, 30, &out_actual_voltage)) != ZX_OK) {
    zxlogf(ERROR, "Unable to request a particular voltage. Got out_voltage as %d",
           out_actual_voltage);
    return status;
  }

  // We registered to the domain with voltage range 50-800. 30 will be rounded to 50.
  if (out_actual_voltage != 50) {
    zxlogf(ERROR, "Generic power driver failed to set correct voltage. Got out_voltage as %d",
           out_actual_voltage);
    return ZX_ERR_INTERNAL;
  }

  // Write a register and read it back
  if ((status = power_write_pmic_ctrl_reg(power, 0x1234, 6)) != ZX_OK) {
    return status;
  }
  if ((status = power_read_pmic_ctrl_reg(power, 0x1234, &value)) != ZX_OK || value != 6) {
    return status;
  }

  if ((status = power_unregister_power_domain(power) != ZX_OK)) {
    zxlogf(ERROR, "Unable to unregister for power domain");
    return status;
  }

  return ZX_OK;
}

static void test_codec_reset_callback(void* ctx, zx_status_t status) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = status;
}

static void test_codec_stop_callback(void* ctx, zx_status_t status) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = status;
}

static void test_codec_start_callback(void* ctx, zx_status_t status) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = status;
}

static void test_codec_get_info_callback(void* ctx, const info_t* info) {
  zx_status_t* out = (zx_status_t*)ctx;
  if (strcmp(info->unique_id, "test_id")) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  if (strcmp(info->manufacturer, "test_man")) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  if (strcmp(info->product_name, "test_product")) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  *out = ZX_OK;
}

static void test_codec_is_bridgeable_callback(void* ctx, bool supports_bridged_mode) {
  zx_status_t* out = (zx_status_t*)ctx;
  if (supports_bridged_mode != true) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  *out = ZX_OK;
}

static void test_codec_set_bridged_mode_callback(void* ctx) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = ZX_OK;
}

static void test_codec_get_dai_formats_callback(void* ctx, zx_status_t status,
                                                const dai_supported_formats_t* formats_list,
                                                size_t formats_count) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = status;
  if (status != ZX_OK) {
    return;
  }
  // Use memcpy() to avoid direct loads to misaligned pointers.
  uint32_t number_of_channels_list[3];
  memcpy(number_of_channels_list, formats_list[1].number_of_channels_list,
         sizeof(number_of_channels_list));
  uint32_t frame_rate;
  memcpy(&frame_rate, formats_list[2].frame_rates_list, sizeof(frame_rate));
  if (formats_count != 3 || formats_list[0].bits_per_sample_count != 3 ||
      formats_list[0].bits_per_sample_list[0] != 1 ||
      formats_list[0].bits_per_sample_list[1] != 99 ||
      formats_list[0].bits_per_sample_list[2] != 253 ||
      formats_list[0].number_of_channels_count != 0 || formats_list[0].frame_rates_count != 0 ||
      formats_list[1].number_of_channels_count != 3 || number_of_channels_list[0] != 0 ||
      number_of_channels_list[1] != 1 || number_of_channels_list[2] != 200 ||
      formats_list[2].frame_rates_count != 1 || frame_rate != 48000) {
    *out = ZX_ERR_INTERNAL;
  }
}

static void test_codec_set_dai_format_callback(void* ctx, zx_status_t status) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = status;
}

static void test_codec_get_gain_format_callback(void* ctx, const gain_format_t* format) {
  zx_status_t* out = (zx_status_t*)ctx;
  if (format->can_agc != true || format->min_gain != -99.99f) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  *out = ZX_OK;
}

static void test_codec_get_gain_state_callback(void* ctx, const gain_state_t* gain_state) {
  zx_status_t* out = (zx_status_t*)ctx;
  if (gain_state->gain != 123.456f || gain_state->muted != true ||
      gain_state->agc_enable != false) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  *out = ZX_OK;
}

static void test_codec_set_gain_state_callback(void* ctx) {
  zx_status_t* out = (zx_status_t*)ctx;
  *out = ZX_OK;
}

static void test_codec_get_plug_state_callback(void* ctx, const plug_state_t* plug_state) {
  zx_status_t* out = (zx_status_t*)ctx;
  if (plug_state->hardwired != false || plug_state->plugged != true) {
    *out = ZX_ERR_INTERNAL;
    return;
  }
  *out = ZX_OK;
}

static zx_status_t test_codec(codec_protocol_t* codec) {
  zx_status_t status = ZX_OK;
  codec_reset(codec, test_codec_reset_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_stop(codec, test_codec_stop_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_start(codec, test_codec_start_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_get_info(codec, test_codec_get_info_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_is_bridgeable(codec, test_codec_is_bridgeable_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_set_bridged_mode(codec, true, test_codec_set_bridged_mode_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_get_dai_formats(codec, test_codec_get_dai_formats_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  dai_format_t format = {};
  codec_set_dai_format(codec, &format, test_codec_set_dai_format_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_get_gain_format(codec, test_codec_get_gain_format_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  codec_get_gain_state(codec, test_codec_get_gain_state_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  gain_state_t gain_state = {};
  codec_set_gain_state(codec, &gain_state, test_codec_set_gain_state_callback, &status);
  if (status != ZX_OK) {
    return status;
  }
  codec_get_plug_state(codec, test_codec_get_plug_state_callback, (void*)&status);
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

static zx_status_t test_pwm(pwm_protocol_t* pwm) {
  zx_status_t status = ZX_OK;
  mode_config_t mode_cfg = {.mode = 0, .magic = {12345}};
  pwm_config_t cfg = {
      false, 1000, 39.0, &mode_cfg, sizeof(mode_cfg),
  };
  if ((status = pwm_set_config(pwm, &cfg)) != ZX_OK) {
    return status;
  }
  mode_config_t out_mode_cfg = {.mode = 0, .magic = {0}};
  pwm_config_t out_config = {
      false, 0, 0.0, &out_mode_cfg, sizeof(out_mode_cfg),
  };
  pwm_get_config(pwm, &out_config);
  if (cfg.polarity != out_config.polarity || cfg.period_ns != out_config.period_ns ||
      cfg.duty_cycle != out_config.duty_cycle ||
      cfg.mode_config_size != out_config.mode_config_size ||
      memcmp(cfg.mode_config_buffer, out_config.mode_config_buffer, cfg.mode_config_size)) {
    return ZX_ERR_INTERNAL;
  }
  if ((status = pwm_enable(pwm)) != ZX_OK) {
    return status;
  }
  if ((status = pwm_disable(pwm)) != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

static zx_status_t test_rpmb(rpmb_protocol_t* rpmb) {
  zx_handle_t client, server;
  zx_status_t status = zx_channel_create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }

  rpmb_connect_server(rpmb, server);
  return zx_handle_close(client);
}

static zx_status_t test_vreg(vreg_protocol_t* vreg) {
  zx_status_t st = ZX_OK;

  st = vreg_set_voltage_step(vreg, 123);
  if (st != ZX_OK) {
    return st;
  }

  uint32_t step = vreg_get_voltage_step(vreg);
  if (step != 123) {
    return ZX_ERR_INTERNAL;
  }

  vreg_params_t params;
  vreg_get_regulator_params(vreg, &params);
  if ((params.min_uv != 123) || (params.step_size_uv != 456) || (params.num_steps != 789)) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

static zx_status_t test_bind(void* ctx, zx_device_t* parent) {
  composite_protocol_t composite;
  zx_status_t status;

  zxlogf(INFO, "test_bind: %s ", DRIVER_NAME);

  status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_COMPOSITE", DRIVER_NAME);
    return status;
  }

  uint32_t count = composite_get_fragment_count(&composite);
  size_t actual;
  composite_device_fragment_t fragments[count];
  composite_get_fragments_new(&composite, fragments, count, &actual);
  if (count != actual) {
    zxlogf(ERROR, "%s: got the wrong number of fragments (%u, %zu)", DRIVER_NAME, count, actual);
    return ZX_ERR_BAD_STATE;
  }

  pdev_protocol_t pdev;
  if (strncmp(fragments[FRAGMENT_PDEV_1].name, "ddk.protocol.platform.device.PDev", 32)) {
    zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_PDEV_1].name);
    return ZX_ERR_INTERNAL;
  }

  status = device_get_protocol(fragments[FRAGMENT_PDEV_1].device, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_PDEV", DRIVER_NAME);
    return status;
  }

  size_t size;
  composite_test_metadata metadata;
  status =
      device_get_metadata_size(fragments[FRAGMENT_PDEV_1].device, DEVICE_METADATA_PRIVATE, &size);
  if (status != ZX_OK || size != sizeof(composite_test_metadata)) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed: %d", DRIVER_NAME, status);
    return ZX_ERR_INTERNAL;
  }
  status = device_get_metadata(fragments[FRAGMENT_PDEV_1].device, DEVICE_METADATA_PRIVATE,
                               &metadata, sizeof(metadata), &size);
  if (status != ZX_OK || size != sizeof(composite_test_metadata)) {
    zxlogf(ERROR, "%s: device_get_metadata failed: %d", DRIVER_NAME, status);
    return ZX_ERR_INTERNAL;
  }

  if (metadata.metadata_value != 12345) {
    zxlogf(ERROR, "%s: device_get_metadata failed: %d", DRIVER_NAME, status);
    return ZX_ERR_INTERNAL;
  }

  clock_protocol_t clock;
  power_protocol_t power;
  clock_protocol_t child4;
  gpio_protocol_t gpio;
  i2c_protocol_t i2c;
  codec_protocol_t codec;
  spi_protocol_t spi;
  pwm_protocol_t pwm;
  rpmb_protocol_t rpmb;
  vreg_protocol_t vreg;
  goldfish_address_space_protocol_t goldfish_address_space;
  goldfish_pipe_protocol_t goldfish_pipe;

  if (metadata.composite_device_id == PDEV_DID_TEST_COMPOSITE_1) {
    if (count != FRAGMENT_COUNT_1) {
      zxlogf(ERROR, "%s: got the wrong number of fragments (%u, %d)", DRIVER_NAME, count,
             FRAGMENT_COUNT_1);
      return ZX_ERR_BAD_STATE;
    }

    if (strncmp(fragments[FRAGMENT_CLOCK_1].name, "clock", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_CLOCK_1].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_CLOCK_1].device, ZX_PROTOCOL_CLOCK, &clock);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_CLOCK", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_POWER_1].name, "power", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_POWER_1].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_POWER_1].device, ZX_PROTOCOL_POWER, &power);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_POWER", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_CHILD4_1].name, "child4", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_CHILD4_1].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_CHILD4_1].device, ZX_PROTOCOL_CLOCK, &child4);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol from child4", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_GPIO_1].name, "gpio", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_GPIO_1].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_GPIO_1].device, ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_GPIO", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_I2C_1].name, "i2c", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_I2C_1].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_I2C_1].device, ZX_PROTOCOL_I2C, &i2c);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_I2C", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_CODEC_1].name, "codec", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_CODEC_1].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_CODEC_1].device, ZX_PROTOCOL_CODEC, &codec);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_CODEC", DRIVER_NAME);
      return status;
    }
    if ((status = test_clock(&clock)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_clock failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_power(&power)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_power failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_gpio(&gpio)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_gpio failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_i2c(&i2c)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_i2c failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_codec(&codec)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_codec failed: %d", DRIVER_NAME, status);
      return status;
    }
  } else if (metadata.composite_device_id == PDEV_DID_TEST_COMPOSITE_2) {
    if (count != FRAGMENT_COUNT_2) {
      zxlogf(ERROR, "%s: got the wrong number of components (%u, %d)", DRIVER_NAME, count,
             FRAGMENT_COUNT_2);
      return ZX_ERR_BAD_STATE;
    }

    if (strncmp(fragments[FRAGMENT_CLOCK_2].name, "clock", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_CLOCK_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_CLOCK_2].device, ZX_PROTOCOL_CLOCK, &clock);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_CLOCK", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_POWER_2].name, "power", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_POWER_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_POWER_2].device, ZX_PROTOCOL_POWER, &power);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_POWER", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_CHILD4_2].name, "child4", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_CHILD4_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_CHILD4_2].device, ZX_PROTOCOL_CLOCK, &child4);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol from child4", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_SPI_2].name, "spi", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_SPI_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_SPI_2].device, ZX_PROTOCOL_SPI, &spi);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_SPI", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_PWM_2].name, "pwm", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_PWM_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_PWM_2].device, ZX_PROTOCOL_PWM, &pwm);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_PWM", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_RPMB_2].name, "rpmb", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_RPMB_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_RPMB_2].device, ZX_PROTOCOL_RPMB, &rpmb);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_RPMB", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_VREG_2].name, "vreg", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME, fragments[FRAGMENT_VREG_2].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_VREG_2].device, ZX_PROTOCOL_VREG, &vreg);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_VREG", DRIVER_NAME);
      return status;
    }

    if ((status = test_clock(&clock)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_clock failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_power(&power)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_power failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_spi(&spi)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_spi failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_pwm(&pwm)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_pwm failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_rpmb(&rpmb)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_rpmb failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_vreg(&vreg)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_vreg failed: %d", DRIVER_NAME, status);
      return status;
    }
  } else if (metadata.composite_device_id == PDEV_DID_TEST_GOLDFISH_CONTROL_COMPOSITE) {
    if (count != FRAGMENT_COUNT_GOLDFISH_CTRL) {
      zxlogf(ERROR, "%s: got the wrong number of fragments (%u, %d)", DRIVER_NAME, count,
             FRAGMENT_COUNT_GOLDFISH_CTRL);
      return ZX_ERR_BAD_STATE;
    }

    if (strncmp(fragments[FRAGMENT_GOLDFISH_ADDRESS_SPACE_GOLDFISH_CTRL].name, "goldfish-address",
                32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME,
             fragments[FRAGMENT_GOLDFISH_ADDRESS_SPACE_GOLDFISH_CTRL].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_GOLDFISH_ADDRESS_SPACE_GOLDFISH_CTRL].device,
                                 ZX_PROTOCOL_GOLDFISH_ADDRESS_SPACE, &goldfish_address_space);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_ADDRESS_SPACE", DRIVER_NAME);
      return status;
    }
    if (strncmp(fragments[FRAGMENT_GOLDFISH_PIPE_GOLDFISH_CTRL].name, "goldfish-pipe", 32)) {
      zxlogf(ERROR, "%s: Unexpected name: %s", DRIVER_NAME,
             fragments[FRAGMENT_GOLDFISH_PIPE_GOLDFISH_CTRL].name);
      return ZX_ERR_INTERNAL;
    }
    status = device_get_protocol(fragments[FRAGMENT_GOLDFISH_PIPE_GOLDFISH_CTRL].device,
                                 ZX_PROTOCOL_GOLDFISH_PIPE, &goldfish_pipe);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_GOLDFISH_PIPE", DRIVER_NAME);
      return status;
    }
    if ((status = test_goldfish_address_space(&goldfish_address_space)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_goldfish_address_space failed: %d", DRIVER_NAME, status);
      return status;
    }
    if ((status = test_goldfish_pipe(&goldfish_pipe)) != ZX_OK) {
      zxlogf(ERROR, "%s: test_goldfish_pipe failed: %d", DRIVER_NAME, status);
      return status;
    }
  }

  test_t* test = calloc(1, sizeof(test_t));
  if (!test) {
    return ZX_ERR_NO_MEMORY;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "composite",
      .ctx = test,
      .ops = &test_device_protocol,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  status = device_add(parent, &args, &test->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_add failed: %d", DRIVER_NAME, status);
    free(test);
    return status;
  }

  // Make sure we can read metadata added to a fragment.
  status = device_get_metadata_size(test->zxdev, DEVICE_METADATA_PRIVATE, &size);
  if (status != ZX_OK || size != sizeof(composite_test_metadata)) {
    zxlogf(ERROR, "%s: device_get_metadata_size failed: %d", DRIVER_NAME, status);
    device_async_remove(test->zxdev);
    return ZX_ERR_INTERNAL;
  }
  status =
      device_get_metadata(test->zxdev, DEVICE_METADATA_PRIVATE, &metadata, sizeof(metadata), &size);
  if (status != ZX_OK || size != sizeof(composite_test_metadata)) {
    zxlogf(ERROR, "%s: device_get_metadata failed: %d", DRIVER_NAME, status);
    device_async_remove(test->zxdev);
    return ZX_ERR_INTERNAL;
  }

  if (metadata.metadata_value != 12345) {
    zxlogf(ERROR, "%s: device_get_metadata failed: %d", DRIVER_NAME, status);
    device_async_remove(test->zxdev);
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(test_bus, test_driver_ops, "zircon", "0.1", 6)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_COMPOSITE_1),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_COMPOSITE_2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_GOLDFISH_CONTROL_COMPOSITE),
ZIRCON_DRIVER_END(test_bus)
