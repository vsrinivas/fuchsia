// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

__BEGIN_CDECLS

// Opaque type representing launchpad state.
// Use of this object is not thread-safe.
typedef struct launchpad launchpad_t;

// API OVERVIEW
// -------------------------------------------------------------
//
// Launchpad is designed to be used like this:
//   launchpad_t* lp;
//   launchpad_create(job, "processname", &lp);
//   launchpad_load_from_file(lp, argv[0]);
//   launchpad_set_args(lp, argc, argv);
//   launchpad_set_environ(lp, env);
//   << other launchpad_*() calls to setup initial fds, handles, etc >>
//   zx_handle_t proc;
//   const char* errmsg;
//   zx_status_t status = launchpad_go(lp, &proc, &errmsg);
//   if (status < 0)
//       printf("launchpad failed: %s: %d\n", errmsg, status);
//
// If any of the calls leading up to launchpad_go(), including
// launchpad_create() itself fail, launchpad_go() will return
// an error, and (if errmsg is non-NULL) provide a human-readable
// descriptive string.
// If proc is NULL, the process handle is closed for you.
//
// There are alternative versions of launchpad_create_*() which
// provide more options, various simple and complex alternatives to
// launchpad_load_*(), and a variety of functions to configure fds,
// handles, etc, which are passed to the new process.  They are
// described in detail below.


// CREATION: one launchpad_create*() below must be called to create
// a launchpad before any other operations may be one with it.
// ----------------------------------------------------------------

// Create a new process and a launchpad that will set it up.
// The job handle is used for creation of the process, but is not
// taken ownership of or closed.
//
// If the job handle is 0 (ZX_HANDLE_INVALID), the default job for
// the running process is used, if it exists (zx_job_default()).
//
// Unless the new process is provided with a job handle, at time
// of launch or later, it will not be able to create any more
// processes.
zx_status_t launchpad_create(zx_handle_t job, const char* name,
                             launchpad_t** lp);

// Create a new process and a launchpad that will set it up.
// The creation_job handle is used to create the process but is
// not taken ownership of or closed.
//
// The transferred_job handle is optional.  If non-zero, it is
// consumed by the launchpad and will be passed to the new process
// on successful launch or closed on failure.
zx_status_t launchpad_create_with_jobs(zx_handle_t creation_job,
                                       zx_handle_t transferred_job,
                                       const char* name,
                                       launchpad_t** result);

// Create a new launchpad for a given existing process handle and
// its root VMAR handle.  On success, the launchpad takes ownership
// of both handles.
zx_status_t launchpad_create_with_process(zx_handle_t proc,
                                          zx_handle_t vmar,
                                          launchpad_t** result);


// LAUNCHING or ABORTING:
// ----------------------------------------------------------------

// If none of the launchpad_*() calls against this launchpad have failed,
// and launchpad_abort() has not been called, this will attempt to complete
// the launch of the process.
//
// If proc is NULL, the process handle is closed instead of returned.
// If errmsg is non-NULL, the human readable status string is returned.
//
// The launchpad is destroyed (via launchpad_destroy()) before this returns,
// all resources are reclaimed, handles are closed, and may not be accessed
// again.
zx_status_t launchpad_go(launchpad_t* lp, zx_handle_t* proc, const char** errmsg);

// Clean up a launchpad_t, freeing all resources stored therein.
// TODO(mcgrathr): Currently this closes the process handle but does
// not kill a process that hasn't been started yet.
void launchpad_destroy(launchpad_t* lp);

// This ensures that the launchpad will not be launchable and
// any calls to launchpad_go() will fail.
// If it is not already in an error state, the error state is
// set to status, and errmsg is set to msg.
// If status is non-negative, it is interpreted as ZX_ERR_INTERNAL.
void launchpad_abort(launchpad_t* lp, zx_status_t status, const char* msg);

// If any launchpad_*() call against this lp has failed, this returns
// a human-readable detailed message describing the failure that may
// assist in debugging.
const char* launchpad_error_message(launchpad_t* lp);
zx_status_t launchpad_get_status(launchpad_t* lp);


// SIMPLIFIED BINARY LOADING
// These functions are convenience wrappers around the more powerful
// Advanced Binary Loading functions described below.  They cover the
// most common use cases.
// -------------------------------------------------------------------

// Load an ELF PIE binary from path
zx_status_t launchpad_load_from_file(launchpad_t* lp, const char* path);

// Load an ELF PIE binary from fd
zx_status_t launchpad_load_from_fd(launchpad_t* lp, int fd);

// Load an ELF PIE binary from vmo
zx_status_t launchpad_load_from_vmo(launchpad_t* lp, zx_handle_t vmo);


// ADDING ARGUMENTS, ENVIRONMENT, AND HANDLES
// These functions setup arguments, environment, or handles to be
// passed to the new process via the processargs protocol.
// ---------------------------------------------------------------------

// Set the arguments, environment, or nametable to be passed in the
// bootstrap message.  All the strings are copied into the launchpad
// by this call, with no pointers to these argument strings retained.
// Successive calls replace the previous values.
zx_status_t launchpad_set_args(launchpad_t* lp,
                               int argc, const char* const* argv);
zx_status_t launchpad_set_environ(launchpad_t* lp, const char* const* envp);
zx_status_t launchpad_set_nametable(launchpad_t* lp,
                                    size_t count, const char* const* names);


// Add one or more handles to be passed in the bootstrap message.
// The launchpad takes ownership of the handles; they will be closed
// by launchpad_destroy or transferred by launchpad_go.
// Successive calls append more handles.  The list of handles to
// send is cleared only by a successful launchpad_go call.
// It is an error to add a handle of 0 (ZX_HANDLE_INVALID)
zx_status_t launchpad_add_handle(launchpad_t* lp, zx_handle_t h, uint32_t id);
zx_status_t launchpad_add_handles(launchpad_t* lp, size_t n,
                                  const zx_handle_t h[], const uint32_t id[]);

// ADDING FDIO FILE DESCRIPTORS
// These functions configure the initial file descriptors, root directory,
// and current working directory for processes which use libfdio for the
// posix-style io api (open/close/read/write/...)
// --------------------------------------------------------------------


// This function allows some or all of the environment of the
// running process to be shared with the process being launched.
// The items shared are as of the call to launchpad_clone().
//
// CLONE_FDIO_NAMESPACE  shares the filestem namespace
// CLONE_FDIO_STDIO      shares file descriptors 0, 1, and 2
// CLONE_ENVIRON         shares the environment
// CLONE_JOB             shares the default job (if one exists)
//
// It is *not* an error if any of the above requested items don't
// exist (eg, fd0 is closed)
//
// launchpad_clone_fd() and launchpad_transfer_fd() may be used to
// add additional file descriptors to the launched process.
#define LP_CLONE_FDIO_NAMESPACE  (0x0001u)
#define LP_CLONE_FDIO_STDIO      (0x0004u)
#define LP_CLONE_FDIO_ALL        (0x00FFu)
#define LP_CLONE_ENVIRON         (0x0100u)
#define LP_CLONE_DEFAULT_JOB     (0x0200u)
#define LP_CLONE_ALL             (0xFFFFu)

zx_status_t launchpad_clone(launchpad_t* lp, uint32_t what);


// Attempt to duplicate local descriptor fd into target_fd in the
// new process.  Returns ZX_ERR_BAD_HANDLE if fd is not a valid fd, or
// ZX_ERR_NOT_SUPPORTED if it's not possible to transfer this fd.
zx_status_t launchpad_clone_fd(launchpad_t* lp, int fd, int target_fd);

// Attempt to transfer local descriptor fd into target_fd in the
// new process.  Returns ZX_ERR_BAD_HANDLE if fd is not a valid fd,
// ERR_UNAVILABLE if fd has been duplicated or is in use in an
// io operation, or ZX_ERR_NOT_SUPPORTED if it's not possible to transfer
// this fd.
// Upon success, from the point of view of the calling process, the fd
// will appear to have been closed.  The underlying "file" will continue
// to exist until launch succeeds (and it is transferred) or fails (and
// it is destroyed).
zx_status_t launchpad_transfer_fd(launchpad_t* lp, int fd, int target_fd);

// Attempt to create a pipe and install one end of that pipe as
// target_fd in the new process and return the other end (if
// successful) via the fd_out parameter.
zx_status_t launchpad_add_pipe(launchpad_t* lp, int* fd_out, int target_fd);


// ACCESSORS for internal state
// --------------------------------------------------------------------

// Fetch the process handle.  The launchpad still owns this handle
// and callers must not close it or transfer it away.
zx_handle_t launchpad_get_process_handle(launchpad_t* lp);

// Fetch the process's root VMAR handle.  The launchpad still owns this handle
// and callers must not close it or transfer it away.
zx_handle_t launchpad_get_root_vmar_handle(launchpad_t* lp);


// ADVANCED BINARY LOADING
// These functions provide advanced control over binary loading.
// -------------------------------------------------------------------

// Map in the PT_LOAD segments of the ELF file image found in a VM
// object.  If the file has a PT_GNU_STACK program header with a
// nonzero p_memsz field, this calls launchpad_set_stack_size with
// that value.  This does not check the file for a PT_INTERP program
// header.  This consumes the VM object.
// If the 'vmo' argument is a negative error code rather
// than a handle, that result is just returned immediately; so this
// can be passed the result of <launchpad/vmo.h> functions without
// separate error checking.
zx_status_t launchpad_elf_load_basic(launchpad_t* lp, zx_handle_t vmo);

// Do general loading of the ELF file image found in a VM object.
// The interface follows the same rules as launchpad_elf_load_basic.
// If the file has no PT_INTERP program header, this behaves the
// same as launchpad_elf_load_basic.  If the file has a PT_INTERP
// string, that string is looked up via the loader service and the
// resulting VM object is loaded instead of the handle passed here,
// which is instead transferred to the dynamic linker in the
// bootstrap message.
zx_status_t launchpad_elf_load(launchpad_t* lp, zx_handle_t vmo);

// Load an extra ELF file image into the process.  This is similar
// to launchpad_elf_load_basic, but it does not consume the VM
// object handle, does affect the state of the launchpad's
// send_loader_message flag, and does not set the entrypoint
// returned by launchpad_get_entry_address and used by
// launchpad_go.  Instead, if base is not NULL, it's filled with
// the address at which the image was loaded; if entry is not NULL,
// it's filled with the image's entrypoint address.
zx_status_t launchpad_elf_load_extra(launchpad_t* lp, zx_handle_t vmo,
                                     zx_vaddr_t* base, zx_vaddr_t* entry);

// Load an executable file into memory. If the file is an ELF file, it
// will be loaded as per launchpad_elf_load. If it is a script (the
// first two characters are "#!"), the next sequence of non-whitespace
// characters in the file specify the name of an interpreter that will
// be loaded instead, using the loader service RPC. Any text that
// follows the interpreter specification on the first line will be passed
// as the first argument to the interpreter, followed by all of the
// original argv arguments (which includes the script name in argv[0]).
// The length of the first line of an interpreted script may not exceed
// 127 characters, or ZX_ERR_NOT_FOUND will be returned. If an invalid vmo
// handle is passed, ZX_ERR_INVALID_ARGS will be returned.
zx_status_t launchpad_file_load(launchpad_t* lp, zx_handle_t vmo);

// The maximum length of the first line of a file that specifies an
// interpreter, using the #! syntax.
#define LP_MAX_INTERP_LINE_LEN 127

// The maximum levels of indirection allowed in script execution.
#define LP_MAX_SCRIPT_NEST_LEVEL 5

// Discover the entry-point address after a successful call to
// launchpad_elf_load or launchpad_elf_load_basic.  This can be used
// in zx_process_start directly rather than calling launchpad_go,
// to bypass sending the standard startup message.
zx_status_t launchpad_get_entry_address(launchpad_t* lp, zx_vaddr_t* entry);

// Return the base address after a successful call to
// launchpad_elf_load or launchpad_elf_load_basic.
zx_status_t launchpad_get_base_address(launchpad_t* lp, zx_vaddr_t* base);

// Set the flag saying whether to send an initial bootstrap message
// for the dynamic linker, and return the old value of the flag.
// This flag is always cleared by launchpad_elf_load_basic and by a
// launchpad_go call that succeeds in sending the message (even
// if it later fails to send the main bootstrap message or fails to
// start the process).  It's set or cleared by launchpad_elf_load
// depending on whether the file has a PT_INTERP program header.
bool launchpad_send_loader_message(launchpad_t* lp, bool do_send);

// Set the handle to the loader service to be used when required,
// and transferred in the initial bootstrap message to the dynamic
// linker.  This consumes the handle passed, and returns the old
// handle (passing ownership of it to the caller).  If no handle has
// been explicitly specified when launchpad_elf_load encounters a
// PT_INTERP header, it will launch fdio_loader_service and install
// that handle (after using it to look up the PT_INTERP string).
zx_handle_t launchpad_use_loader_service(launchpad_t* lp, zx_handle_t svc);

// This duplicates the globally-held VM object handle for the system
// vDSO.  The return value is that of zx_handle_duplicate.  If
// launchpad_set_vdso_vmo has been called with a valid handle, this
// just duplicates the handle passed in the last call.  Otherwise,
// the first time the system vDSO is needed it's fetched with
// zx_get_startup_handle.
zx_status_t launchpad_get_vdso_vmo(zx_handle_t* out);

// Replace the globally-held VM object handle for the system vDSO.
// This takes ownership of the given handle, and returns the old
// handle, of which the caller takes ownership.  It does not check
// the handle for validity.  If ZX_HANDLE_INVALID is passed here,
// then the next time the system vDSO is needed it will be fetched
// with zx_get_startup_handle as if it were the first time.  If
// the system vDSO has not been needed before this call, then the
// return value will be ZX_HANDLE_INVALID.
zx_handle_t launchpad_set_vdso_vmo(zx_handle_t vmo);

// Add the VM object handle for the system vDSO to the launchpad, so
// the launched process will be able to load it into its own
// children.  This is just shorthand for launchpad_add_handle with
// the handle returned by launchpad_get_vdso_vmo.
zx_status_t launchpad_add_vdso_vmo(launchpad_t* lp);

// Load the system vDSO into the launchpad's nascent process.  The
// given handle is not consumed.  If given ZX_HANDLE_INVALID, this
// uses the VM object that launchpad_get_vdso_vmo would return
// instead.  This just calls launchpad_elf_load_extra to do the
// loading, and records the vDSO's base address for launchpad_go
// to pass to the new process's initial thread.
zx_status_t launchpad_load_vdso(launchpad_t* lp, zx_handle_t vmo);

// Set the size of the initial thread's stack, and return the old setting.
// The initial setting after launchpad_create is a system default.
// If this is passed zero, then there will be no stack allocated.
// Otherwise, the size passed is rounded up to a multiple of the page size.
size_t launchpad_set_stack_size(launchpad_t* lp, size_t new_size);




// Start a new thread in the process, assuming this was a launchpad
// created with launchpad_create_with_process and the process has
// already started.  The new thread runs the launchpad's entry point
// just like the initial thread does in the launchpad_go case.
// The given handle is to a channel where the bootstrap
// messages will be written; the caller retains ownership of this
// handle.  The other end of this channel must already be
// present in the target process, with the given handle value in the
// target process's handle space.
zx_status_t launchpad_start_injected(launchpad_t* lp, const char* thread_name,
                                     zx_handle_t to_child,
                                     uintptr_t bootstrap_handle_in_child);






__END_CDECLS
