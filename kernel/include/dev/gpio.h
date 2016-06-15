// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008, Google Inc.
// Copyright (c) 2008-2012 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


#ifndef __DEV_GPIO_H
#define __DEV_GPIO_H

enum gpio_flags {
    GPIO_INPUT      = (1 << 0),
    GPIO_OUTPUT     = (1 << 1),
    GPIO_LEVEL      = (1 << 2),
    GPIO_EDGE       = (1 << 3),
    GPIO_RISING     = (1 << 4),
    GPIO_FALLING    = (1 << 5),
    GPIO_HIGH       = (1 << 6),
    GPIO_LOW        = (1 << 7),
    GPIO_PULLUP     = (1 << 8),
    GPIO_PULLDOWN   = (1 << 9),
};

/* top 16 bits of the gpio flags are platform specific */
#define GPIO_PLATFORM_MASK 0xffff0000

int gpio_config(unsigned nr, unsigned flags);
void gpio_set(unsigned nr, unsigned on);
int gpio_get(unsigned nr);

#endif
