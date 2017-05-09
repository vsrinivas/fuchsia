// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#pragma GCC visibility push(hidden)

#include <magenta/types.h>
#include <stddef.h>

struct bootfs;

// Returns the base address (p_vaddr bias).
mx_vaddr_t elf_load_vmo(mx_handle_t log, mx_handle_t vmar, mx_handle_t vmo);

// Returns the entry point address in the child, either to the named
// executable or to the PT_INTERP file loaded instead.  If the main
// file has a PT_INTERP, that name (with a fixed prefix applied) is
// also found in the bootfs and loaded instead of the main
// executable.  In that case, an extra mx_proc_args_t message is
// sent down the to_child pipe to prime the interpreter (presumably
// the dynamic linker) with the given log handle and a VMO for the
// main executable and a loader-service channel, the other end of
// which is returned here.
mx_vaddr_t elf_load_bootfs(mx_handle_t log, struct bootfs *fs,
                           mx_handle_t proc, mx_handle_t vmar,
                           mx_handle_t thread,
                           const char* filename, mx_handle_t to_child,
                           size_t* stack_size, mx_handle_t* loader_svc);

#pragma GCC visibility pop
