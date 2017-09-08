// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifdef __Fuchsia__
// TODO(smklein): Move thse ioctls to public/magenta/fs/vfs.h
#include <magenta/device/device.h>
#include <magenta/device/vfs.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <magenta/types.h>
#endif

#include <fs/trace.h>
#include <fs/vfs.h>

#include <fcntl.h>
#include <string.h>
#ifdef __Fuchsia__
#include <threads.h>
#endif
#include <unistd.h>

#include <mxio/remoteio.h>

#ifndef __Fuchsia__
#define O_NOREMOTE 0100000000
#endif
