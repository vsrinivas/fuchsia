// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <magenta/mdi.h>
#include <mdi/mdi.h>

__BEGIN_CDECLS

// called at platform early init time
// pass node reference to list of kernel drivers to init
void pdev_init(const mdi_node_ref_t* drivers);

__END_CDECLS
