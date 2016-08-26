// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <platform/pc/acpi.h>

bool hpet_is_present(void);

void hpet_enable(void);
void hpet_disable(void);

void hpet_wait_ms(uint16_t ms);
