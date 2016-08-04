// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#pragma GCC visibility push(hidden)

#include <magenta/types.h>

struct bootfs;

// Returns the base address (p_vaddr bias).
mx_vaddr_t elf_load_vmo(mx_handle_t log, mx_handle_t proc, mx_handle_t vmo);

// Returns the entry point address in the child, either to the named
// executable or to the PT_INTERP file loaded instead.  If the main
// file has a PT_INTERP, that name (with a fixed prefix applied) is
// also found in the bootfs and loaded instead of the main
// executable.  In that case, an extra mx_proc_args_t message is
// sent down the to_child pipe to prime the interpreter (presumably
// the dynamic linker) with the given log handle and a VMO for the
// main executable.
mx_vaddr_t elf_load_bootfs(mx_handle_t log, struct bootfs *fs,
                           mx_handle_t proc, const char* filename,
                           mx_handle_t to_child);

#pragma GCC visibility pop
