// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008, Google Inc.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#ifndef __DEV_GPIO_KEYPAD_H
#define __DEV_GPIO_KEYPAD_H

#include <sys/types.h>

/* unset: drive active output low, set: drive active output high */
#define GPIOKPF_ACTIVE_HIGH     (1U << 0)
#define GPIOKPF_DRIVE_INACTIVE      (1U << 1)

struct gpio_keypad_info {
    /* size must be ninputs * noutputs */
    const uint16_t *keymap;
    unsigned *input_gpios;
    unsigned *output_gpios;
    int ninputs;
    int noutputs;
    /* time to wait before reading inputs after driving each output */
    time_t settle_time;
    time_t poll_time;
    unsigned flags;
};

void gpio_keypad_init(struct gpio_keypad_info *kpinfo);

#endif /* __DEV_GPIO_KEYPAD_H */
