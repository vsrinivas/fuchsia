// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <target.h>

#include "target_p.h"

void target_init(void)
{
    pc_debug_init();
}

