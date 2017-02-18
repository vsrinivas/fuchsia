// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/syscalls/types.h>

#include <magenta/syscalls/pci.h>
#include <magenta/syscalls/resource.h>

__BEGIN_CDECLS

#include <magenta/gen-syscalls.h>

__END_CDECLS

// Compatibility Wrappers for Deprecated Syscalls

// For now, much of Fucshia assumes the contents of this header still live here.
// TODO(kulakowski)(MG-540) Remove this include.
#include <magenta/process.h>
