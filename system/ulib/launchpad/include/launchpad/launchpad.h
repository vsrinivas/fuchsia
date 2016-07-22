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

#include <magenta/types.h>
#include <system/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

// Opaque type representing launchpad state.
// Use of this object is not thread-safe.
typedef struct launchpad launchpad_t;

// Create a new process and a launchpad that will set it up.
mx_status_t launchpad_create(const char* name, launchpad_t** lp);

// Clean up a launchpad_t, freeing all resources stored therein.
// TODO(mcgrathr): Currently this closes the process handle but does
// not kill a process that hasn't been started yet.
void launchpad_destroy(launchpad_t* lp);

// Fetch the process handle.  The launchpad still owns this handle
// and callers must not close it or transfer it away.
mx_handle_t launchpad_get_process_handle(launchpad_t* lp);

// Add one or more handles to be passed in the bootstrap message.
// The launchpad takes ownership of the handles; they will be closed
// by launchpad_destroy or transferred by launchpad_start.
// Successive calls append more handles.  The list of handles to
// send is cleared only by a successful launchpad_start call.
mx_status_t launchpad_add_handle(launchpad_t* lp, mx_handle_t h, uint32_t id);
mx_status_t launchpad_add_handles(launchpad_t* lp, size_t n,
                                  const mx_handle_t h[], const uint32_t id[]);

// Set the arguments or environment to be passed in the bootstrap
// message.  All the strings are copied into the launchpad by this
// call, with no pointers to these argument strings retained.
// Successive calls replace the previous values.
mx_status_t launchpad_arguments(launchpad_t* lp, int argc, char** argv);
mx_status_t launchpad_environ(launchpad_t* lp, char** envp);

// Clone the mxio root handle into the new process.
// This will allow mxio-based filesystem access to work in the new process.
mx_status_t launchpad_clone_mxio_root(launchpad_t* lp);

// Attempt to duplicate local descriptor fd into target_fd in the
// new process.  Returns ERR_BAD_HANDLE if fd is not a valid fd, or
// ERR_NOT_SUPPORTED if it's not possible to transfer this fd.
mx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd);

// Map in the PT_LOAD segments of the ELF file image found in a VM
// object.  This does not check the file for a PT_INTERP program
// header.  This consumes the VM object handle on success but not on
// failure.  If the 'vmo' argument is a negative error code rather
// than a handle, that result is just returned immediately; so this
// can be passed the result of <launchpad/vmo.h> functions without
// separate error checking.
mx_status_t launchpad_elf_load_basic(launchpad_t* lp, mx_handle_t vmo);

// Do general loading of the ELF file image found in a VM object.
// The interface follows the same rules as launchpad_elf_load_basic.
// If the file has no PT_INTERP program header, this behaves the
// same as launchpad_elf_load_basic.  If the file has a PT_INTERP
// string, that string is looked up via the loader service and the
// resulting VM object is loaded instead of the handle passed here,
// which is instead transferred to the dynamic linker in the
// bootstrap message.
mx_status_t launchpad_elf_load(launchpad_t* lp, mx_handle_t vmo);

// Set the flag saying whether to send an initial bootstrap message
// for the dynamic linker, and return the old value of the flag.
// This flag is always cleared by launchpad_elf_load_basic and by a
// launchpad_start call that succeeds in sending the message (even
// if it later fails to send the main bootstrap message or fails to
// start the process).  It's set or cleared by launchpad_elf_load
// depending on whether the file has a PT_INTERP program header.
bool launchpad_send_loader_message(launchpad_t* lp, bool do_send);

// Set the handle to the loader service to be used when required,
// and transferred in the initial bootstrap message to the dynamic
// linker.  This consumes the handle passed, and returns the old
// handle (passing ownership of it to the caller).  If no handle has
// been explicitly specified when launchpad_elf_load encounters a
// PT_INTERP header, it will launch mxio_loader_service and install
// that handle (after using it to look up the PT_INTERP string).
mx_handle_t launchpad_use_loader_service(launchpad_t* lp, mx_handle_t svc);

// Start the process running.  If the send_loader_message flag is
// set and this succeeds in sending the initial bootstrap message,
// it clears the loader-service handle.  If this succeeds in sending
// the main bootstrap message, it clears the list of handles to
// transfer (after they've been transferred) as well as the process
// handle.  The return value doesn't distinguish failure to send the
// first or second message from failure to start the process, so on
// failure the loader-service handle might or might not have been
// cleared and the handles to transfer might or might not have been
// cleared.
mx_status_t launchpad_start(launchpad_t* lp);

__END_CDECLS
