// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform-defs.h>

#include <zircon/types.h>

// https://www.nxp.com/docs/en/data-sheet/PCA9956A.pdf
// See 7.3 Register definitions
#define PCA9956_MODE_REGISTER_1 0x00
#define PCA9956_MODE_REGISTER_2 0x01
#define PCA9956_DUTY_CYCLE 0x08
#define PCA9956_PWM_BASE 0x0A
#define PCA9956_IREF_BASE 0x22
#define PCA9956_IREFALL 0x40

// If bit is set in an address, reads and writes will auto increment.
// See 7.2 Control register
#define AUTO_INCREMENT_ADDRESS_MASK 0x80

#define NUM_PWM_CHANNELS 24

// 3 decimal digits + 1 whitespace
#define UINT8_PRINT_SIZE 4

typedef struct gauss_led {
    // All i2c operations should take the lock to make sure reset is atomic.
    mtx_t lock;
    zx_device_t* device;
    i2c_protocol_t i2c;
    i2c_channel_t channel;
} gauss_led_t;

// Driver subpath types
typedef enum path_type {
    PATH_NONE,
    PATH_RESET,
    PATH_PWM,
    PATH_DUTY_CYCLE,
} path_type_t;

static const struct {
    const char* str;
    path_type_t path;
} kPathStrToPath[] = {
    {"reset", PATH_RESET},
    {"pwm", PATH_PWM},
    {"duty_cycle", PATH_DUTY_CYCLE},
};

// Device instance
typedef struct gauss_led_dev {
    path_type_t path;
    gauss_led_t* led;
    iotxn_t* txn;
} gauss_led_dev_t;

// Attempts to parse a decmial uint8 from |*buf|. Will update |buf| if parsing
// is successful. Will skip whitespace before and after the number.
static int parse_uint8(char** buf) {
    unsigned value;
    int num_chars;
    if (sscanf(*buf, "%u%n", &value, &num_chars) != 1) {
        return -1;
    }
    if (value > UINT8_MAX) {
        return -1;
    }

    *buf += num_chars;
    while (isspace(**buf)) {
        ++(*buf);
    }
    return value;
}

static void gauss_led_handle_read_complete(zx_status_t status,
                                           const uint8_t* data,
                                           size_t actual,
                                           gauss_led_dev_t* dev,
                                           iotxn_t* txn) {
    char buf[256] = {};
    switch (dev->path) {
    case PATH_PWM: {
        if (actual < NUM_PWM_CHANNELS) {
            zxlogf(ERROR, "Failed to read pcm channels\n");
            iotxn_complete(txn, ZX_ERR_INTERNAL, 0);
            return;
        }
        size_t remain = sizeof(buf);
        char* cur = buf;
        for (size_t i = 0; i < actual; ++i) {
            int ret = snprintf(cur, remain, "%hhu ", data[i]);
            if (ret < 0) {
                zxlogf(ERROR, "snprintf failed\n");
                iotxn_complete(txn, ZX_ERR_INTERNAL, 0);
                return;
            }

            cur += ret;
            remain -= ret;
        }
        *(cur - 1) = '\n';
        break;
    }
    case PATH_DUTY_CYCLE: {
        if (actual < 1) {
            zxlogf(ERROR, "Failed to read duty cycle value\n");
            iotxn_complete(txn, ZX_ERR_INTERNAL, 0);
            return;
        }
        int len = snprintf(buf, sizeof(buf), "%hhu\n", data[0]);
        if (len < 0) {
            zxlogf(ERROR, "snprintf failed\n");
            iotxn_complete(txn, ZX_ERR_INTERNAL, 0);
            return;
        }
        break;
    }
    default:
        assert(false);
    }

    size_t len = strlen(buf);
    assert(len <= txn->length);
    iotxn_copyto(txn, buf, len, 0);
    iotxn_complete(txn, ZX_OK, len);
}

static void i2c_complete(zx_status_t status, const uint8_t* data, size_t actual,
                         void* cookie) {
    gauss_led_dev_t* dev = cookie;
    if (!dev) {
        if (status != ZX_OK) {
            zxlogf(ERROR, "i2c transaction failed\n");
        }
        return;
    }
    assert(dev->txn);
    iotxn_t* txn = dev->txn;
    dev->txn = NULL;
    if (txn->opcode == IOTXN_OP_READ) {
        gauss_led_handle_read_complete(status, data, actual, dev, txn);
    } else if (txn->opcode == IOTXN_OP_WRITE) {
        iotxn_complete(txn, status, txn->length);
    } else {
        zxlogf(ERROR, "Unexpected transaction type\n");
        assert(false);
    }
}

static zx_status_t gauss_led_i2c_transact(gauss_led_t* gauss_led,
                                          uint8_t* write_buf,
                                          size_t write_len,
                                          size_t read_len,
                                          gauss_led_dev_t* dev) {
    mtx_lock(&gauss_led->lock);
    zx_status_t status = i2c_transact(&gauss_led->channel, write_buf, write_len,
                                      read_len, i2c_complete, dev);
    mtx_unlock(&gauss_led->lock);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to write to I2C: %d\n", (int)status);
        return status;
    }

    return status;
}

static zx_status_t gauss_led_reset_helper(gauss_led_t* gauss_led) {
    zx_status_t status = ZX_OK;

    // Set max gain control for all IREF registers.
    {
        uint8_t buf[] = {PCA9956_IREFALL, 0xff};
        if ((status = i2c_transact(&gauss_led->channel, buf, sizeof(buf), 0,
                                   i2c_complete, NULL)) != ZX_OK) {
            return status;
        }
    }

    // Enable auto-increment for registers 00h to 39h.
    {
        uint8_t buf[] = {PCA9956_MODE_REGISTER_1, 0x40};
        if ((status = i2c_transact(&gauss_led->channel, buf, sizeof(buf), 0,
                                   i2c_complete, NULL)) != ZX_OK) {
            return status;
        }
    }

    // Reset MODE2
    // Set LEDOUT0 - LEDOUT5 to max
    // Initialize GRPPWM (pwm duty cycle)
    {
        uint8_t buf[] = {PCA9956_MODE_REGISTER_2 | AUTO_INCREMENT_ADDRESS_MASK,
                         0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x08};
        if ((status = i2c_transact(&gauss_led->channel, buf, sizeof(buf), 0,
                                   i2c_complete, NULL)) != ZX_OK) {
            return status;
        }
    }

    // Turn off all LEDs
    uint8_t buf[NUM_PWM_CHANNELS + 1] =
        {PCA9956_PWM_BASE | AUTO_INCREMENT_ADDRESS_MASK};
    if ((status = i2c_transact(&gauss_led->channel, buf, sizeof(buf), 0,
                               i2c_complete, NULL)) != ZX_OK) {
        return status;
    }

    return status;
}

static zx_status_t gauss_led_reset(gauss_led_t* gauss_led) {
    mtx_lock(&gauss_led->lock);
    zx_status_t status = gauss_led_reset_helper(gauss_led);
    mtx_unlock(&gauss_led->lock);

    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to initialize LED controller: %d\n", (int)status);
        return status;
    }

    return status;
}

static zx_status_t gauss_led_dev_get_pwm(gauss_led_dev_t* dev, iotxn_t* txn) {
    size_t min_len = NUM_PWM_CHANNELS * UINT8_PRINT_SIZE;
    if (txn->length < min_len) {
        zxlogf(ERROR, "Read is too short, must be atleast %zd\n", min_len);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    uint8_t write_buf[] = {PCA9956_PWM_BASE | AUTO_INCREMENT_ADDRESS_MASK};

    zx_status_t status = gauss_led_i2c_transact(dev->led, write_buf,
                                                sizeof(write_buf),
                                                NUM_PWM_CHANNELS, dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to get pwm values: %d\n", (int)status);
        return status;
    }

    return status;
}

static zx_status_t gauss_led_dev_set_pwm(gauss_led_dev_t* dev, iotxn_t* txn) {
    char buf[512];
    if (txn->length > sizeof(buf) - 1) {
        zxlogf(ERROR, "Write is too long\n");
        return ZX_ERR_INVALID_ARGS;
    }

    ssize_t ret = iotxn_copyfrom(txn, buf, sizeof(buf), 0);
    if (ret < 0) {
        zxlogf(ERROR, "Failed to copy data\n");
        return ZX_ERR_INTERNAL;
    }
    buf[txn->length] = '\0';

    uint8_t write_buf[NUM_PWM_CHANNELS + 1] =
        {PCA9956_PWM_BASE | AUTO_INCREMENT_ADDRESS_MASK};
    size_t idx = 0;

    char* cur = buf;
    while (*cur) {
        if (idx >= NUM_PWM_CHANNELS) {
            zxlogf(ERROR, "Too many values, expected %d\n", NUM_PWM_CHANNELS);
            return ZX_ERR_INVALID_ARGS;
        }
        int val = parse_uint8(&cur);
        if (val < 0) {
            zxlogf(ERROR, "Invalid RGB value\n");
            return ZX_ERR_INVALID_ARGS;
        }

        write_buf[1 + idx] = val;
        ++idx;
    }

    if (idx != NUM_PWM_CHANNELS) {
        zxlogf(ERROR, "Not enough values, expected %d\n", NUM_PWM_CHANNELS);
        return ZX_ERR_INVALID_ARGS;
    }

    zx_status_t status = gauss_led_i2c_transact(dev->led, write_buf,
                                                sizeof(write_buf), 0, dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to set LEDs: %d\n", (int)status);
        return status;
    }

    return status;
}

static zx_status_t gauss_led_dev_get_duty_cycle(gauss_led_dev_t* dev,
                                                iotxn_t* txn) {
    size_t min_len = UINT8_PRINT_SIZE;
    if (txn->length < min_len) {
        zxlogf(ERROR, "Read is too short, must be atleast %d\n",
               UINT8_PRINT_SIZE);
        return ZX_ERR_BUFFER_TOO_SMALL;
    }
    uint8_t write_buf[] = {PCA9956_DUTY_CYCLE};
    zx_status_t status = gauss_led_i2c_transact(dev->led, write_buf,
                                                sizeof(write_buf), 1, dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to get pwm values: %d\n", (int)status);
        return status;
    }

    return status;
}

static zx_status_t gauss_led_dev_set_duty_cycle(gauss_led_dev_t* dev,
                                                iotxn_t* txn) {
    char buf[64];
    if (txn->length > sizeof(buf) - 1) {
        zxlogf(ERROR, "Write is too long\n");
        return ZX_ERR_INVALID_ARGS;
    }

    ssize_t ret = iotxn_copyfrom(txn, buf, sizeof(buf), 0);
    if (ret < 0) {
        zxlogf(ERROR, "Failed to copy data\n");
        return ZX_ERR_INTERNAL;
    }
    buf[txn->length] = '\0';
    char* cur = buf;
    int value = parse_uint8(&cur);
    if (value < -1) {
        zxlogf(ERROR, "Invalid value\n");
        return ZX_ERR_INVALID_ARGS;
    }
    if (*cur != '\0') {
        zxlogf(ERROR, "Trailing data\n");
        return ZX_ERR_INVALID_ARGS;
    }

    uint8_t write_buf[] = {PCA9956_DUTY_CYCLE, value};
    zx_status_t status = gauss_led_i2c_transact(dev->led, write_buf,
                                                sizeof(write_buf), 0, dev);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to set LEDs: %d\n", (int)status);
        return status;
    }

    return status;
}

static void gauss_led_dev_handle_read(gauss_led_dev_t* dev, iotxn_t* txn) {
    zx_status_t status = ZX_OK;
    switch (dev->path) {
    case PATH_RESET:
        status = ZX_ERR_NOT_SUPPORTED;
        break;
    case PATH_PWM:
        status = gauss_led_dev_get_pwm(dev, txn);
        break;
    case PATH_DUTY_CYCLE:
        status = gauss_led_dev_get_duty_cycle(dev, txn);
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
        zxlogf(ERROR, "Unsupported path type: %d\n", (int)dev->path);
        assert(false);
        break;
    }

    if (status != ZX_OK) {
        iotxn_complete(txn, status, 0);
    }
}

static void gauss_led_dev_handle_write(gauss_led_dev_t* dev, iotxn_t* txn) {
    zx_status_t status = ZX_OK;
    switch (dev->path) {
    case PATH_RESET:
        // For resets, we don't want to block iotxn_complete on the i2c writes.
        dev->txn = NULL;
        iotxn_complete(txn, gauss_led_reset(dev->led), txn->length);
        return;
    case PATH_PWM:
        status = gauss_led_dev_set_pwm(dev, txn);
        break;
    case PATH_DUTY_CYCLE:
        status = gauss_led_dev_set_duty_cycle(dev, txn);
        break;
    default:
        status = ZX_ERR_NOT_SUPPORTED;
        zxlogf(ERROR, "Unsupported path type: %d\n", (int)dev->path);
        assert(false);
        break;
    }

    if (status != ZX_OK) {
        iotxn_complete(txn, status, 0);
    }
}

static void gauss_led_dev_iotxn_queue(void* ctx, iotxn_t* txn) {
    if (txn->offset > 0) {
        iotxn_complete(txn, ZX_OK, 0);
        return;
    }
    gauss_led_dev_t* dev = ctx;
    if (dev->txn != NULL) {
        zxlogf(ERROR, "Transaction already pending\n");
        iotxn_complete(txn, ZX_ERR_BAD_STATE, 0);
        return;
    }
    dev->txn = txn;

    if (txn->opcode == IOTXN_OP_READ) {
        gauss_led_dev_handle_read(dev, txn);
    } else if (txn->opcode == IOTXN_OP_WRITE) {
        gauss_led_dev_handle_write(dev, txn);
    } else {
        iotxn_complete(txn, ZX_ERR_INVALID_ARGS, 0);
    }
}

static void gauss_led_dev_release(void* ctx) {
    gauss_led_dev_t* dev = ctx;
    free(dev);
}

static zx_protocol_device_t gauss_led_dev_ops = {
    .version = DEVICE_OPS_VERSION,
    .iotxn_queue = gauss_led_dev_iotxn_queue,
    .release = gauss_led_dev_release,
};

static zx_status_t gauss_led_open_at(void* ctx, zx_device_t** dev_out,
                                     const char* path, uint32_t flags) {
    gauss_led_t* gauss_led = ctx;
    path_type_t path_type = PATH_NONE;
    for (size_t i = 0; i < countof(kPathStrToPath); ++i) {
        if (strcmp(kPathStrToPath[i].str, path) == 0) {
            path_type = kPathStrToPath[i].path;
            break;
        }
    }

    if (path_type == PATH_NONE) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    gauss_led_dev_t* dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ZX_ERR_NO_MEMORY;
    }

    dev->path = path_type;
    dev->led = gauss_led;

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "led",
        .ctx = dev,
        .ops = &gauss_led_dev_ops,
        .flags = DEVICE_ADD_INSTANCE,
    };
    zx_device_t* tmp_out;
    zx_status_t status = device_add(gauss_led->device, &args, &tmp_out);
    if (status != ZX_OK) {
        free(dev);
        return status;
    }

    *dev_out = tmp_out;
    return ZX_OK;
}

static void gauss_led_release(void* ctx) {
    gauss_led_t* gauss_led = ctx;
    i2c_channel_release(&gauss_led->channel);
    free(gauss_led);
}

static zx_protocol_device_t gauss_led_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .open_at = gauss_led_open_at,
    .release = gauss_led_release,
};

static zx_status_t gauss_led_bind(void* ctx, zx_device_t* parent) {
    gauss_led_t* gauss_led = calloc(1, sizeof(*gauss_led));
    if (!gauss_led) {
        return ZX_ERR_NO_MEMORY;
    }

    if (device_get_protocol(parent, ZX_PROTOCOL_I2C,
                            &gauss_led->i2c) != ZX_OK) {
        free(gauss_led);
        return ZX_ERR_NOT_SUPPORTED;
    }

    zx_status_t status = i2c_get_channel(&gauss_led->i2c, 0,
                                         &gauss_led->channel);
    if (status != ZX_OK) {
        free(gauss_led);
        zxlogf(ERROR, "Failed to get channel: %d\n", (int)status);
        return status;
    }

    device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "gauss-led",
        .ctx = gauss_led,
        .ops = &gauss_led_device_protocol,
    };

    if ((status = device_add(parent, &args, &gauss_led->device)) != ZX_OK) {
        free(gauss_led);
        return status;
    }

    if ((status = gauss_led_reset(gauss_led)) != ZX_OK) {
        free(gauss_led);
        return status;
    }

    return status;
}

static zx_driver_ops_t i2c_led_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = gauss_led_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(gauss_i2c_led, i2c_led_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GAUSS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GAUSS_LED),
ZIRCON_DRIVER_END(gauss_i2c_led)
    // clang-format on
