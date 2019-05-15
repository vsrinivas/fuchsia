// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GRPC_FDIO_UTIL_H_
#define SRC_VIRTUALIZATION_LIB_GRPC_FDIO_UTIL_H_

#include <lib/zx/socket.h>

int ConvertSocketToNonBlockingFd(zx::socket socket);

#endif  // SRC_VIRTUALIZATION_LIB_GRPC_FDIO_UTIL_H_
