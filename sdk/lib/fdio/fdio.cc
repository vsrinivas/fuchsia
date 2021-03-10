// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fdio.h>
#include <lib/zxio/ops.h>

#include "internal.h"

fdio::~fdio() = default;

__EXPORT
extern "C" zxio_t* fdio_get_zxio(fdio_t* io) { return &io->zxio_storage().io; }
