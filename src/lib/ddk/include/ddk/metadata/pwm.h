// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_PWM_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_PWM_H_

#include <zircon/types.h>
/*
      id - unique id of PWM channel
      protect - if true, the pwm channel will not be initialized and driver
                will not allow access to the channel.  This is for use in
                situations where the pwm channel may be under control of
                higher exception level or a bootloader configuration must
                be preserved.  (for a dvfs rail for example)
*/
typedef struct {
  uint32_t id;
  bool protect = false;
} pwm_id_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_PWM_H_
