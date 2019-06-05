// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/metadata.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/clock.h>
#include <ddk/protocol/codec.h>
#include <ddk/protocol/composite.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c-lib.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/power.h>
#include <zircon/assert.h>

#define DRIVER_NAME "test-composite"

enum {
    COMPONENT_PDEV,
    COMPONENT_GPIO,
    COMPONENT_CLOCK,
    COMPONENT_I2C,
    COMPONENT_POWER,
    COMPONENT_CHILD4,
    COMPONENT_CODEC,
    COMPONENT_COUNT,
};

typedef struct {
    zx_device_t* zxdev;
} test_t;

static void test_release(void* ctx) {
    free(ctx);
}

static zx_protocol_device_t test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = test_release,
};

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

    if ((status = clock_enable(clock)) != ZX_OK) {
        return status;
    }
    if ((status = clock_disable(clock)) != ZX_OK) {
        return status;
    }

    return ZX_OK;
}

static zx_status_t test_i2c(i2c_protocol_t* i2c) {
    size_t max_transfer;

    // i2c test driver returns 1024 for max transfer size
    zx_status_t status = i2c_get_max_transfer_size(i2c, &max_transfer);
    if (status != ZX_OK || max_transfer != 1024) {
        zxlogf(ERROR, "%s: i2c_get_max_transfer_size failed\n", DRIVER_NAME);
        return ZX_ERR_INTERNAL;
    }

    // i2c test driver reverses digits
    const uint32_t write_digits[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    uint32_t read_digits[10];
    memset(read_digits, 0, sizeof(read_digits));

    status = i2c_write_read_sync(i2c, write_digits, sizeof(write_digits), read_digits,
                                 sizeof(read_digits));
    if (status != ZX_OK || max_transfer != 1024) {
        zxlogf(ERROR, "%s: i2c_write_read_sync failed %d\n", DRIVER_NAME, status);
        return status;
    }

    for (size_t i = 0; i < countof(read_digits); i++) {
        if (read_digits[i] != write_digits[countof(read_digits) - i - 1]) {
            zxlogf(ERROR, "%s: read_digits does not match reverse of write digits\n", DRIVER_NAME);
            return ZX_ERR_INTERNAL;
        }
    }

    return ZX_OK;
}

static zx_status_t test_power(power_protocol_t* power) {
    zx_status_t status;
    uint32_t value;

    // Write a register and read it back
    if ((status = power_write_pmic_ctrl_reg(power, 0x1234, 6)) != ZX_OK) {
        return status;
    }
    if ((status = power_read_pmic_ctrl_reg(power, 0x1234, &value)) != ZX_OK || value != 6) {
        return status;
    }

    return ZX_OK;
}

static void test_codec_reset_callback(void* ctx, zx_status_t status) {
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
    if (formats_count != 3 ||
        formats_list[0].bits_per_sample_count != 3 ||
        formats_list[0].bits_per_sample_list[0] != 1 ||
        formats_list[0].bits_per_sample_list[1] != 99 ||
        formats_list[0].bits_per_sample_list[2] != 253 ||
        formats_list[0].number_of_channels_count != 0 ||
        formats_list[0].frame_rates_count != 0 ||
        formats_list[1].number_of_channels_count != 3 ||
        formats_list[1].number_of_channels_list[0] != 0 ||
        formats_list[1].number_of_channels_list[1] != 1 ||
        formats_list[1].number_of_channels_list[2] != 200 ||
        formats_list[2].frame_rates_count != 1 ||
        formats_list[2].frame_rates_list[0] != 48000) {
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

static zx_status_t test_bind(void* ctx, zx_device_t* parent) {
    composite_protocol_t composite;
    zx_status_t status;

    zxlogf(INFO, "test_bind: %s \n", DRIVER_NAME);

    status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_COMPOSITE\n", DRIVER_NAME);
        return status;
    }

    uint32_t count = composite_get_component_count(&composite);
    size_t actual;
    zx_device_t* components[count];
    composite_get_components(&composite, components, count, &actual);
    if (count != actual || count != COMPONENT_COUNT) {
        zxlogf(ERROR, "%s: got the wrong number of components (%u, %zu)\n", DRIVER_NAME, count,
               actual);
        return ZX_ERR_BAD_STATE;
    }

    pdev_protocol_t pdev;
    gpio_protocol_t gpio;
    clock_protocol_t clock;
    i2c_protocol_t i2c;
    power_protocol_t power;
    clock_protocol_t child4;
    codec_protocol_t codec;

    status = device_get_protocol(components[COMPONENT_PDEV], ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_PDEV\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_GPIO], ZX_PROTOCOL_GPIO, &gpio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_GPIO\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_CLOCK], ZX_PROTOCOL_CLOCK, &clock);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_CLOCK\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_I2C], ZX_PROTOCOL_I2C, &i2c);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_I2C\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_POWER], ZX_PROTOCOL_POWER, &power);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_POWER\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_CHILD4], ZX_PROTOCOL_CLOCK, &child4);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol from child4\n", DRIVER_NAME);
        return status;
    }
    status = device_get_protocol(components[COMPONENT_CODEC], ZX_PROTOCOL_CODEC, &codec);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: could not get protocol ZX_PROTOCOL_CODEC\n", DRIVER_NAME);
        return status;
    }

    if ((status = test_gpio(&gpio)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_gpio failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    if ((status = test_clock(&clock)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_clock failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    if ((status = test_i2c(&i2c)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_i2c failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    if ((status = test_power(&power)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_power failed: %d\n", DRIVER_NAME, status);
        return status;
    }

    if ((status = test_codec(&codec)) != ZX_OK) {
        zxlogf(ERROR, "%s: test_codec failed: %d\n", DRIVER_NAME, status);
        return status;
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
        zxlogf(ERROR, "%s: device_add failed: %d\n", DRIVER_NAME, status);
        free(test);
        return status;
    }

    // Make sure we can read metadata added to a component.
    size_t size;
    uint32_t value;
    status = device_get_metadata_size(test->zxdev, DEVICE_METADATA_PRIVATE, &size);
    if (status != ZX_OK || size != sizeof(value)) {
        zxlogf(ERROR, "%s: device_get_metadata_size failed: %d\n", DRIVER_NAME, status);
        device_remove(test->zxdev);
        return ZX_ERR_INTERNAL;
    }
    status = device_get_metadata(test->zxdev, DEVICE_METADATA_PRIVATE, &value, sizeof(value),
                                 &size);
    if (status != ZX_OK || size != sizeof(value) || value != 12345) {
        zxlogf(ERROR, "%s: device_get_metadata failed: %d\n", DRIVER_NAME, status);
        device_remove(test->zxdev);
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

ZIRCON_DRIVER_BEGIN(test_bus, test_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_TEST),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_PBUS_TEST),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_TEST_COMPOSITE),
ZIRCON_DRIVER_END(test_bus)
