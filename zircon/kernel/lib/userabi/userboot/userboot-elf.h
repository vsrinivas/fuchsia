// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_USERBOOT_USERBOOT_ELF_H_
#define ZIRCON_KERNEL_LIB_USERABI_USERBOOT_USERBOOT_ELF_H_

#include <stddef.h>
#include <zircon/types.h>

struct bootfs;

// Returns the base address (p_vaddr bias).
zx_vaddr_t elf_load_vmo(zx_handle_t log, zx_handle_t vmar, zx_handle_t vmo);

// Returns the entry point address in the child, either to the named
// executable or to the PT_INTERP file loaded instead.  If the main
// file has a PT_INTERP, that name (with a fixed prefix applied) is
// also found in the bootfs and loaded instead of the main
// executable.  In that case, an extra zx_proc_args_t message is
// sent down the to_child pipe to prime the interpreter (presumably
// the dynamic linker) with the given log handle and a VMO for the
// main executable and a loader-service channel, the other end of
// which is returned here.
zx_vaddr_t elf_load_bootfs(zx_handle_t log, struct bootfs* fs, const char* root_prefix,
                           zx_handle_t proc, zx_handle_t vmar, zx_handle_t thread,
                           const char* filename, zx_handle_t to_child, size_t* stack_size,
                           zx_handle_t* loader_svc);

#endif  // ZIRCON_KERNEL_LIB_USERABI_USERBOOT_USERBOOT_ELF_H_
