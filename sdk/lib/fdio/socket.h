// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_SOCKET_H_
#define LIB_FDIO_SOCKET_H_

#include <fbl/ref_ptr.h>

struct fdio;

fbl::RefPtr<fdio> fdio_socket_allocate();

#endif  // LIB_FDIO_SOCKET_H_
