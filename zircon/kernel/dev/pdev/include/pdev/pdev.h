// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_PDEV_INCLUDE_PDEV_PDEV_H_
#define ZIRCON_KERNEL_DEV_PDEV_INCLUDE_PDEV_PDEV_H_

#include <zircon/boot/image.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

// called at platform early init time
void pdev_init();

__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_PDEV_INCLUDE_PDEV_PDEV_H_
