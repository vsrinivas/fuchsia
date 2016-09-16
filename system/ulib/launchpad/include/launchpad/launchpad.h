// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/types.h>
#include <magenta/compiler.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

// Opaque type representing launchpad state.
// Use of this object is not thread-safe.
typedef struct launchpad launchpad_t;

// Create a new process and a launchpad that will set it up.
mx_status_t launchpad_create(const char* name, launchpad_t** lp);

// Create a new launchpad for a given existing process handle.
// On success, the launchpad takes ownership of the process handle.
mx_status_t launchpad_create_with_process(mx_handle_t proc,
                                          launchpad_t** result);

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
mx_status_t launchpad_arguments(launchpad_t* lp,
                                int argc, const char* const* argv);
mx_status_t launchpad_environ(launchpad_t* lp, const char* const* envp);

// Clone the mxio root handle into the new process.
// This will allow mxio-based filesystem access to work in the new process.
mx_status_t launchpad_clone_mxio_root(launchpad_t* lp);

// Clone the mxio current working directory handle into the new process
// This will allow mxio-based filesystem access to inherit the cwd from the
// launching process. If mxio root is cloned but not mxio cwd, mxio root is
// treated as the cwd.
mx_status_t launchpad_clone_mxio_cwd(launchpad_t* lp);

// Attempt to duplicate local descriptor fd into target_fd in the
// new process.  Returns ERR_BAD_HANDLE if fd is not a valid fd, or
// ERR_NOT_SUPPORTED if it's not possible to transfer this fd.
mx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd);

// Convenience function to add all mxio handles to the launchpad.
// This calls launchpad_clone_mxio_root and then launchpad_clone_fd for each
// fd in the calling process.
mx_status_t launchpad_add_all_mxio(launchpad_t* lp);

// Attempt to create a pipe and install one end of that pipe as
// target_fd in the new process and return the other end (if
// successful) via the fd_out parameter.
mx_status_t launchpad_add_pipe(launchpad_t* lp, int* fd_out, int target_fd);

// Map in the PT_LOAD segments of the ELF file image found in a VM
// object.  If the file has a PT_GNU_STACK program header with a
// nonzero p_memsz field, this calls launchpad_set_stack_size with
// that value.  This does not check the file for a PT_INTERP program
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

// Load an extra ELF file image into the process.  This is similar
// to launchpad_elf_load_basic, but it does not consume the VM
// object handle, does affect the state of the launchpad's
// send_loader_message flag, and does not set the entrypoint
// returned by launchpad_get_entry_address and used by
// launchpad_start.  Instead, if base is not NULL, it's filled with
// the address at which the image was loaded; if entry is not NULL,
// it's filled with the image's entrypoint address.
mx_status_t launchpad_elf_load_extra(launchpad_t* lp, mx_handle_t vmo,
                                     mx_vaddr_t* base, mx_vaddr_t* entry);

// Discover the entry-point address after a successful call to
// launchpad_elf_load or launchpad_elf_load_basic.  This can be used
// in mx_process_start directly rather than calling launchpad_start,
// to bypass sending the standard startup message.
mx_status_t launchpad_get_entry_address(launchpad_t* lp, mx_vaddr_t* entry);

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

// This duplicates the globally-held VM object handle for the system
// vDSO.  The return value is that of mx_handle_duplicate.  If
// launchpad_set_vdso_vmo has been called with a valid handle, this
// just duplicates the handle passed in the last call.  Otherwise,
// the first time the system vDSO is needed it's fetched with
// mxio_get_startup_handle.
mx_handle_t launchpad_get_vdso_vmo(void);

// Replace the globally-held VM object handle for the system vDSO.
// This takes ownership of the given handle, and returns the old
// handle, of which the caller takes ownership.  It does not check
// the handle for validity.  If MX_HANDLE_INVALID is passed here,
// then the next time the system vDSO is needed it will be fetched
// with mxio_get_startup_handle as if it were the first time.  If
// the system vDSO has not been needed before this call, then the
// return value will be MX_HANDLE_INVALID.
mx_handle_t launchpad_set_vdso_vmo(mx_handle_t vmo);

// Add the VM object handle for the system vDSO to the launchpad, so
// the launched process will be able to load it into its own
// children.  This is just shorthand for launchpad_add_handle with
// the handle returned by launchpad_get_vdso_vmo.
mx_status_t launchpad_add_vdso_vmo(launchpad_t* lp);

// Load the system vDSO into the launchpad's nascent process.  The
// given handle is not consumed.  If given MX_HANDLE_INVALID, this
// uses the VM object that launchpad_get_vdso_vmo would return
// instead.  This just calls launchpad_elf_load_extra to do the
// loading, and records the vDSO's base address for launchpad_start
// to pass to the new process's initial thread.
mx_status_t launchpad_load_vdso(launchpad_t* lp, mx_handle_t vmo);

// Set the size of the initial thread's stack, and return the old setting.
// The initial setting after launchpad_create is a system default.
// If this is passed zero, then there will be no stack allocated.
// Otherwise, the size passed is rounded up to a multiple of the page size.
size_t launchpad_set_stack_size(launchpad_t* lp, size_t new_size);

// Start the process running.  If the send_loader_message flag is
// set and this succeeds in sending the initial bootstrap message,
// it clears the loader-service handle.  If this succeeds in sending
// the main bootstrap message, it clears the list of handles to
// transfer (after they've been transferred) as well as the process
// handle.
//
// Returns the process handle on success, giving ownership to the
// caller.  On failure, the return value doesn't distinguish failure
// to send the first or second message from failure to start the
// process, so on failure the loader-service handle might or might
// not have been cleared and the handles to transfer might or might
// not have been cleared.
mx_handle_t launchpad_start(launchpad_t* lp);

// Start a new thread in the process, assuming this was a launchpad
// created with launchpad_create_with_process and the process has
// already started.  The new thread runs the launchpad's entry point
// just like the initial thread does in the launchpad_start case.
// The given handle is to a message pipe where the bootstrap
// messages will be written; the caller retains ownership of this
// handle.  The other end of this message pipe must already be
// present in the target process, with the given handle value in the
// target process's handle space.
mx_status_t launchpad_start_injected(launchpad_t* lp, const char* thread_name,
                                     mx_handle_t to_child,
                                     uintptr_t bootstrap_handle_in_child);

// Convenience interface for launching a process in one call with
// minimal arguments and handles.  This just calls the functions
// launchpad_create, launchpad_elf_load, launchpad_load_vdso,
// launchpad_arguments, launchpad_environ, launchpad_add_handles,
// launchpad_start, launchpad_destroy.
//
// Returns the process handle on success, giving ownership to the caller;
// or an error code on failure.  In all cases, the handles are consumed.
mx_handle_t launchpad_launch(const char* name,
                             int argc, const char* const* argv,
                             const char* const* envp,
                             size_t hnds_count, mx_handle_t* handles,
                             uint32_t* ids);

// Convenience interface for launching a process in one call with
// details inherited from the calling process (environment
// variables, mxio root, and mxio file descriptors).  This just
// calls the functions launchpad_create, launchpad_elf_load,
// launchpad_load_vdso, launchpad_add_vdso_vmo, launchpad_arguments,
// launchpad_environ, launchpad_clone_mxio_root, launchpad_clone_fd,
// launchpad_start, launchpad_destroy.
//
// Returns the process handle on success, giving ownership to the caller;
// or an error code on failure.
mx_handle_t launchpad_launch_mxio(const char* name,
                                  int argc, const char* const* argv);

// Same as launchpad_launch_mxio, but also passes additional handles
// like launchpad_launch, and uses envp rather than global environ.
// In all cases, the handles are consumed.
mx_handle_t launchpad_launch_mxio_etc(const char* name,
                                      int argc, const char* const* argv,
                                      const char* const* envp,
                                      size_t hnds_count, mx_handle_t* handles,
                                      uint32_t* ids);

__END_CDECLS
