// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_ZXIO_REMOTE_V2_DIRENT_ITERATOR_H_
#define ZIRCON_SYSTEM_ULIB_ZXIO_REMOTE_V2_DIRENT_ITERATOR_H_

#include <lib/zxio/ops.h>

zx_status_t zxio_remote_v2_dirent_iterator_init(zxio_t* directory,
                                                zxio_dirent_iterator_t* iterator);

zx_status_t zxio_remote_v2_dirent_iterator_next(zxio_t* io, zxio_dirent_iterator_t* iterator,
                                                zxio_dirent_t** out_entry);

void zxio_remote_v2_dirent_iterator_destroy(zxio_t* io, zxio_dirent_iterator_t* iterator);

#endif  // ZIRCON_SYSTEM_ULIB_ZXIO_REMOTE_V2_DIRENT_ITERATOR_H_
