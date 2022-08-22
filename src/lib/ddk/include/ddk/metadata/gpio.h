// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_GPIO_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_GPIO_H_

#include <stdint.h>

#define GPIO_NAME_MAX_LENGTH (64)  // Actual (GPIO_NAME_MAX_LENGTH-1) characters

#define DECL_GPIO_PIN(pin) \
  { pin, #pin }
#define DECL_GPIO_PIN_WITH_NAME(pin, name) \
  { pin, name }

typedef struct {
  uint32_t pin;
  char name[GPIO_NAME_MAX_LENGTH];
} gpio_pin_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_GPIO_H_
