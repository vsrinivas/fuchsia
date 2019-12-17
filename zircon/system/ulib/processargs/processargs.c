// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/processargs/processargs.h>
#include <zircon/syscalls.h>

// TODO(mcgrathr): Is there a better error code to use for marshalling
// protocol violations?
#define MALFORMED ZX_ERR_INVALID_ARGS

zx_status_t processargs_message_size(zx_handle_t channel, uint32_t* nbytes, uint32_t* nhandles) {
  zx_status_t status = _zx_channel_read(channel, 0, NULL, NULL, 0, 0, nbytes, nhandles);
  if (status == ZX_ERR_BUFFER_TOO_SMALL)
    status = ZX_OK;
  return status;
}

zx_status_t processargs_read(zx_handle_t bootstrap, void* buffer, uint32_t nbytes,
                             zx_handle_t handles[], uint32_t nhandles, zx_proc_args_t** pargs,
                             uint32_t** handle_info) {
  if (nbytes < sizeof(zx_proc_args_t))
    return ZX_ERR_INVALID_ARGS;
  if ((uintptr_t)buffer % alignof(zx_proc_args_t) != 0)
    return ZX_ERR_INVALID_ARGS;

  uint32_t got_bytes = 0;
  uint32_t got_handles = 0;
  zx_status_t status =
      _zx_channel_read(bootstrap, 0, buffer, handles, nbytes, nhandles, &got_bytes, &got_handles);
  if (status != ZX_OK)
    return status;
  if (got_bytes != nbytes || got_handles != nhandles)
    return ZX_ERR_INVALID_ARGS;

  zx_proc_args_t* const pa = buffer;

  if (pa->protocol != ZX_PROCARGS_PROTOCOL || pa->version != ZX_PROCARGS_VERSION)
    return MALFORMED;

  if (pa->handle_info_off < sizeof(*pa) || pa->handle_info_off % alignof(uint32_t) != 0 ||
      pa->handle_info_off > nbytes || (nbytes - pa->handle_info_off) / sizeof(uint32_t) < nhandles)
    return MALFORMED;

  if (pa->args_num > 0 && (pa->args_off < sizeof(*pa) || pa->args_off > nbytes ||
                           (nbytes - pa->args_off) < pa->args_num))
    return MALFORMED;

  if (pa->environ_num > 0 && (pa->environ_off < sizeof(*pa) || pa->environ_off > nbytes ||
                              (nbytes - pa->environ_off) < pa->environ_num))
    return MALFORMED;

  *pargs = pa;
  *handle_info = (void*)&((uint8_t*)buffer)[pa->handle_info_off];
  return ZX_OK;
}

void processargs_extract_handles(uint32_t nhandles, zx_handle_t handles[], uint32_t handle_info[],
                                 zx_handle_t* process_self, zx_handle_t* job_default,
                                 zx_handle_t* vmar_root_self, zx_handle_t* thread_self,
                                 zx_handle_t* utc_reference) {
  // Find the handles we're interested in among what we were given.
  for (uint32_t i = 0; i < nhandles; ++i) {
    switch (PA_HND_TYPE(handle_info[i])) {
      case PA_PROC_SELF:
        // The handle will have been installed already by dynamic
        // linker startup, but now we have another one.  They
        // should of course be handles to the same process, but
        // just for cleanliness switch to the "main" one.
        if (*process_self != ZX_HANDLE_INVALID)
          _zx_handle_close(*process_self);
        *process_self = handles[i];
        handles[i] = ZX_HANDLE_INVALID;
        handle_info[i] = 0;
        break;

      case PA_JOB_DEFAULT:
        // The default job provided to the process to use for
        // creation of additional processes.  It may or may not
        // be the job this process is a child of.  It may not
        // be provided at all.
        if (*job_default != ZX_HANDLE_INVALID)
          _zx_handle_close(*job_default);
        *job_default = handles[i];
        handles[i] = ZX_HANDLE_INVALID;
        handle_info[i] = 0;
        break;

      case PA_VMAR_ROOT:
        // As above for PROC_SELF
        if (*vmar_root_self != ZX_HANDLE_INVALID)
          _zx_handle_close(*vmar_root_self);
        *vmar_root_self = handles[i];
        handles[i] = ZX_HANDLE_INVALID;
        handle_info[i] = 0;
        break;

      case PA_THREAD_SELF:
        *thread_self = handles[i];
        handles[i] = ZX_HANDLE_INVALID;
        handle_info[i] = 0;
        break;

      case PA_CLOCK_UTC:
        // Do not leak handles if our launcher was foolish enough to pass us
        // multiple UTC references.
        if (*utc_reference != ZX_HANDLE_INVALID) {
          _zx_handle_close(*utc_reference);
        }
        *utc_reference = handles[i];
        handles[i] = ZX_HANDLE_INVALID;
        handle_info[i] = 0;
        break;
    }
  }
}

static zx_status_t unpack_strings(char* buffer, uint32_t bytes, char* result[], uint32_t off,
                                  uint32_t num) {
  char* p = &buffer[off];
  for (uint32_t i = 0; i < num; ++i) {
    result[i] = p;
    do {
      if (p >= &buffer[bytes])
        return MALFORMED;
    } while (*p++ != '\0');
  }
  result[num] = NULL;
  return ZX_OK;
}

zx_status_t processargs_strings(void* msg, uint32_t bytes, char* argv[], char* envp[],
                                char* names[]) {
  zx_proc_args_t* const pa = msg;
  zx_status_t status = ZX_OK;
  if (argv != NULL) {
    status = unpack_strings(msg, bytes, argv, pa->args_off, pa->args_num);
  }
  if (envp != NULL && status == ZX_OK) {
    status = unpack_strings(msg, bytes, envp, pa->environ_off, pa->environ_num);
  }
  if (names != NULL && status == ZX_OK) {
    status = unpack_strings(msg, bytes, names, pa->names_off, pa->names_num);
  }
  return status;
}
