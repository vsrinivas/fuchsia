// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/elf-psabi/sp.h>
#include <lib/fdio/io.h>
#include <lib/fidl/txn_header.h>
#include <lib/zircon-internal/default_stack_size.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/dlfcn.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <ldmsg/ldmsg.h>

#include "elf.h"

enum special_handles { HND_LDSVC_LOADER, HND_EXEC_VMO, HND_SEGMENTS_VMAR, HND_SPECIAL_COUNT };

struct launchpad {
  uint32_t argc;
  uint32_t envc;
  uint32_t namec;
  char* args;
  size_t args_len;
  char* env;
  size_t env_len;
  char* names;
  size_t names_len;

  zx_handle_t* handles;
  uint32_t* handles_info;
  size_t handle_count;
  size_t handle_alloc;

  const char* errmsg;
  zx_status_t error;

  zx_vaddr_t entry;
  zx_vaddr_t base;
  zx_vaddr_t vdso_base;

  size_t stack_size;
  bool set_stack_size;

  zx_handle_t special_handles[HND_SPECIAL_COUNT];
  bool loader_message;

  zx_handle_t reserve_vmar;
  bool fresh_process;
};

// Returned when calloc() fails on create, so callers
// can still call all the various add handles functions
// which will discard the handles, etc, etc.
static launchpad_t invalid_launchpad = {
    .errmsg = "create: could not allocate launchpad_t",
    .error = ZX_ERR_NO_MEMORY,
};

static zx_status_t lp_error(launchpad_t* lp, zx_status_t error, const char* msg) {
  if (lp->error == ZX_OK) {
    lp->error = error;
    lp->errmsg = msg;
  }
  return lp->error;
}

__EXPORT
zx_status_t launchpad_get_status(launchpad_t* lp) { return lp->error; }

__EXPORT
void launchpad_abort(launchpad_t* lp, zx_status_t error, const char* msg) {
  lp_error(lp, (error < 0) ? error : ZX_ERR_INTERNAL, msg);
}

__EXPORT
const char* launchpad_error_message(launchpad_t* lp) { return lp->errmsg; }

#define HND_LOADER_COUNT 3
// We always install the process handle as the first in the message.
#define lp_proc(lp) ((lp)->handles[0])
// We always install the vmar handle as the second in the message.
#define lp_vmar(lp) ((lp)->handles[1])

__EXPORT
void launchpad_destroy(launchpad_t* lp) {
  if (lp == &invalid_launchpad)
    return;
  zx_handle_close(lp->reserve_vmar);
  zx_handle_close_many(lp->special_handles, HND_SPECIAL_COUNT);
  zx_handle_close_many(lp->handles, lp->handle_count);
  free(lp->handles);
  free(lp->handles_info);
  free(lp->args);
  free(lp->env);
  free(lp->names);
  free(lp);
}

// Create a new launchpad for a given existing process handle and
// its root VMAR handle.  On success, the launchpad takes ownership
// of both handles.
static zx_status_t launchpad_create_with_process(zx_handle_t proc, zx_handle_t vmar,
                                                 launchpad_t** result) {
  launchpad_t* lp = calloc(1, sizeof(*lp));
  if (lp == NULL) {
    lp = &invalid_launchpad;
  } else {
    lp->errmsg = "no error";
  }

  if (launchpad_add_handle(lp, proc, PA_PROC_SELF) == ZX_OK) {
    // If the process has an existing vDSO mapping, record it for
    // use by launchpad_start_extra.
    zx_status_t status = zx_object_get_property(proc, ZX_PROP_PROCESS_VDSO_BASE_ADDRESS,
                                                &lp->vdso_base, sizeof(lp->vdso_base));
    if (status != ZX_OK)
      lp_error(lp, status, "create: cannot get ZX_PROP_PROCESS_VDSO_BASE_ADDRESS");
  }
  launchpad_add_handle(lp, vmar, PA_VMAR_ROOT);

  *result = lp;
  return lp->error;
}

// Create a new process and a launchpad that will set it up.
__EXPORT
zx_status_t launchpad_create_with_jobs(zx_handle_t creation_job, zx_handle_t transferred_job,
                                       const char* name, launchpad_t** result) {
  uint32_t name_len = strlen(name);

  zx_handle_t proc = ZX_HANDLE_INVALID;
  zx_handle_t vmar = ZX_HANDLE_INVALID;
  zx_status_t status = zx_process_create(creation_job, name, name_len, 0, &proc, &vmar);

  launchpad_t* lp;
  if (launchpad_create_with_process(proc, vmar, &lp) == ZX_OK)
    lp->fresh_process = true;

  if (status != ZX_OK) {
    lp->error = ZX_OK;
    lp_error(lp, status, "create: zx_process_create() failed");
  }

  if (transferred_job != ZX_HANDLE_INVALID) {
    launchpad_add_handle(lp, transferred_job, PA_JOB_DEFAULT);
  }

  *result = lp;
  return lp->error;
}

__EXPORT
zx_status_t launchpad_create(zx_handle_t job, const char* name, launchpad_t** result) {
  if (job == ZX_HANDLE_INVALID)
    job = zx_job_default();
  zx_handle_t xjob = ZX_HANDLE_INVALID;
  zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &xjob);
  return launchpad_create_with_jobs(job, xjob, name, result);
}

__EXPORT
zx_handle_t launchpad_get_process_handle(launchpad_t* lp) { return lp_proc(lp); }

__EXPORT
zx_handle_t launchpad_get_root_vmar_handle(launchpad_t* lp) { return lp_vmar(lp); }

static zx_status_t build_stringtable(launchpad_t* lp, int count, const char* const* item,
                                     size_t* total_out, char** out) {
  if (lp->error)
    return lp->error;
  if (count < 0)
    return lp_error(lp, ZX_ERR_INVALID_ARGS, "negative string array count");

  size_t total = 0;
  for (int i = 0; i < count; ++i)
    total += strlen(item[i]) + 1;

  char* buffer = NULL;
  if (total > 0) {
    buffer = malloc(total);
    if (buffer == NULL)
      return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory for string array");

    char* p = buffer;
    for (int i = 0; i < count; ++i)
      p = stpcpy(p, item[i]) + 1;

    if ((size_t)(p - buffer) != total) {
      // The strings changed in parallel.  Not kosher!
      free(buffer);
      return lp_error(lp, ZX_ERR_INVALID_ARGS, "string array modified during use");
    }
  }

  *total_out = total;
  *out = buffer;
  return ZX_OK;
}

__EXPORT
zx_status_t launchpad_set_args(launchpad_t* lp, int argc, const char* const* argv) {
  size_t total;
  char* buffer;
  zx_status_t r = build_stringtable(lp, argc, argv, &total, &buffer);
  if (r < 0)
    return r;

  free(lp->args);
  lp->argc = argc;
  lp->args = buffer;
  lp->args_len = total;
  return ZX_OK;
}

__EXPORT
zx_status_t launchpad_set_nametable(launchpad_t* lp, size_t count, const char* const* names) {
  size_t total;
  char* buffer;
  zx_status_t r = build_stringtable(lp, count, names, &total, &buffer);
  if (r < 0)
    return r;

  free(lp->names);
  lp->namec = count;
  lp->names = buffer;
  lp->names_len = total;
  return ZX_OK;
}

__EXPORT
zx_status_t launchpad_set_environ(launchpad_t* lp, const char* const* envp) {
  uint32_t count = 0;
  if (envp != NULL) {
    for (const char* const* ep = envp; *ep != NULL; ++ep) {
      ++count;
    }
  }

  size_t total;
  char* buffer;
  zx_status_t r = build_stringtable(lp, count, envp, &total, &buffer);
  if (r < 0)
    return r;

  free(lp->env);
  lp->envc = count;
  lp->env = buffer;
  lp->env_len = total;
  return ZX_OK;
}

static zx_status_t more_handles(launchpad_t* lp, size_t n) {
  if (lp->error)
    return lp->error;

  if (ZX_CHANNEL_MAX_MSG_HANDLES - lp->handle_count < n)
    return lp_error(lp, ZX_ERR_NO_MEMORY, "too many handles for handle table");

  if (lp->handle_alloc - lp->handle_count < n) {
    size_t alloc = lp->handle_alloc == 0 ? 8 : lp->handle_alloc * 2;
    while (alloc - lp->handle_count < n)
      alloc <<= 1;
    zx_handle_t* handles = realloc(lp->handles, alloc * sizeof(handles[0]));
    if (handles == NULL)
      return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory for handle table");
    lp->handles = handles;
    uint32_t* info = realloc(lp->handles_info, alloc * sizeof(info[0]));
    if (info == NULL)
      return lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory for handle table");
    lp->handles_info = info;
    lp->handle_alloc = alloc;
  }
  return ZX_OK;
}

__EXPORT
zx_status_t launchpad_add_handle(launchpad_t* lp, zx_handle_t h, uint32_t id) {
  if (h == ZX_HANDLE_INVALID)
    return lp_error(lp, ZX_ERR_BAD_HANDLE, "added invalid handle");
  zx_status_t status = more_handles(lp, 1);
  if (status == ZX_OK) {
    lp->handles[lp->handle_count] = h;
    lp->handles_info[lp->handle_count] = id;
    ++lp->handle_count;
  } else {
    zx_handle_close(h);
  }
  return status;
}

__EXPORT
zx_status_t launchpad_add_handles(launchpad_t* lp, size_t n, const zx_handle_t h[],
                                  const uint32_t id[]) {
  zx_status_t status = more_handles(lp, n);
  if (status == ZX_OK) {
    memcpy(&lp->handles[lp->handle_count], h, n * sizeof(h[0]));
    memcpy(&lp->handles_info[lp->handle_count], id, n * sizeof(id[0]));
    lp->handle_count += n;
    for (size_t i = 0; i < n; i++) {
      if (h[i] == ZX_HANDLE_INVALID) {
        return lp_error(lp, ZX_ERR_BAD_HANDLE, "added invalid handle");
      }
    }
  } else {
    for (size_t i = 0; i < n; i++) {
      zx_handle_close(h[i]);
    }
  }
  return status;
}

static void check_elf_stack_size(launchpad_t* lp, elf_load_info_t* elf) {
  size_t elf_stack_size = elf_load_get_stack_size(elf);
  if (elf_stack_size > 0)
    launchpad_set_stack_size(lp, elf_stack_size);
}

__EXPORT
zx_status_t launchpad_elf_load_basic(launchpad_t* lp, zx_handle_t vmo) {
  if (vmo == ZX_HANDLE_INVALID)
    return lp_error(lp, ZX_ERR_INVALID_ARGS, "elf_load: invalid vmo");
  if (lp->error)
    goto done;

  elf_load_info_t* elf;
  zx_status_t status;
  if ((status = elf_load_start(vmo, NULL, 0, &elf)))
    lp_error(lp, status, "elf_load: elf_load_start() failed");
  zx_handle_t segments_vmar;
  if ((status = elf_load_finish(lp_vmar(lp), elf, vmo, &segments_vmar, &lp->base, &lp->entry)))
    lp_error(lp, status, "elf_load: elf_load_finish() failed");
  check_elf_stack_size(lp, elf);
  elf_load_destroy(elf);

  if (status == ZX_OK) {
    lp->loader_message = false;
    launchpad_add_handle(lp, segments_vmar, PA_HND(PA_VMAR_LOADED, 0));
  }

done:
  zx_handle_close(vmo);
  return lp->error;
}

__EXPORT
zx_status_t launchpad_elf_load_extra(launchpad_t* lp, zx_handle_t vmo, zx_vaddr_t* base,
                                     zx_vaddr_t* entry) {
  if (lp->error)
    return lp->error;
  if (vmo == ZX_HANDLE_INVALID)
    return lp_error(lp, ZX_ERR_INVALID_ARGS, "elf_load_extra: invalid vmo");

  elf_load_info_t* elf;
  zx_status_t status;
  if ((status = elf_load_start(vmo, NULL, 0, &elf)))
    lp_error(lp, status, "elf_load_extra: elf_load_start() failed");
  if ((status = elf_load_finish(lp_vmar(lp), elf, vmo, NULL, base, entry)))
    lp_error(lp, status, "elf_load_extra: elf_load_finish() failed");
  elf_load_destroy(elf);

  return lp->error;
}

#define LOADER_SVC_MSG_MAX 1024

static zx_status_t loader_svc_rpc(zx_handle_t loader_svc, uint64_t ordinal, const void* data,
                                  size_t len, zx_handle_t* out) {
  static _Atomic zx_txid_t next_txid;

  ldmsg_req_t req;
  fidl_init_txn_header(&req.header, atomic_fetch_add(&next_txid, 1), ordinal);

  size_t req_len;
  zx_status_t status = ldmsg_req_encode(&req, &req_len, data, len);
  if (status != ZX_OK)
    return status;

  ldmsg_rsp_t rsp;
  memset(&rsp, 0, sizeof(rsp));

  zx_handle_t handle = ZX_HANDLE_INVALID;
  const zx_channel_call_args_t call = {
      .wr_bytes = &req,
      .wr_num_bytes = req_len,
      .rd_bytes = &rsp,
      .rd_num_bytes = sizeof(rsp),
      .rd_handles = &handle,
      .rd_num_handles = 1,
  };
  uint32_t reply_size;
  uint32_t handle_count;
  status = zx_channel_call(loader_svc, 0, ZX_TIME_INFINITE, &call, &reply_size, &handle_count);
  if (status != ZX_OK)
    return status;

  // Check for protocol violations.
  if (reply_size != ldmsg_rsp_get_size(&rsp)) {
  protocol_violation:
    zx_handle_close(handle);
    return ZX_ERR_BAD_STATE;
  }
  if (rsp.header.ordinal != ordinal)
    goto protocol_violation;

  if (rsp.rv != ZX_OK) {
    if (handle != ZX_HANDLE_INVALID)
      goto protocol_violation;
    if (rsp.rv > 0)
      goto protocol_violation;
    *out = ZX_HANDLE_INVALID;
  } else {
    *out = handle_count ? handle : ZX_HANDLE_INVALID;
  }
  return rsp.rv;
}

static zx_status_t setup_loader_svc(launchpad_t* lp) {
  if (lp->special_handles[HND_LDSVC_LOADER] != ZX_HANDLE_INVALID)
    return ZX_OK;

  zx_handle_t loader_svc;
  zx_status_t status = dl_clone_loader_service(&loader_svc);
  if (status < 0)
    return status;

  lp->special_handles[HND_LDSVC_LOADER] = loader_svc;
  return ZX_OK;
}

// Reserve roughly the low half of the address space, so the new
// process can use sanitizers that need to allocate shadow memory there.
// The reservation VMAR is kept around just long enough to make sure all
// the initial allocations (mapping in the initial ELF objects, and
// allocating the initial stack) stay out of this area, and then destroyed.
// The process's own allocations can then use the full address space; if
// it's using a sanitizer, it will set up its shadow memory first thing.
static zx_status_t reserve_low_address_space(launchpad_t* lp) {
  if (lp->reserve_vmar != ZX_HANDLE_INVALID)
    return ZX_OK;

  zx_info_vmar_t info;
  zx_status_t status =
      zx_object_get_info(lp_vmar(lp), ZX_INFO_VMAR, &info, sizeof(info), NULL, NULL);
  if (status != ZX_OK) {
    return lp_error(lp, status, "zx_object_get_info failed on child root VMAR handle");
  }

  uintptr_t addr;
  size_t reserve_size = (((info.base + info.len) / 2) + PAGE_SIZE - 1) & -PAGE_SIZE;
  status = zx_vmar_allocate(lp_vmar(lp), ZX_VM_SPECIFIC, 0, reserve_size - info.base,
                            &lp->reserve_vmar, &addr);
  if (status != ZX_OK) {
    return lp_error(lp, status, "zx_vmar_allocate failed for low address space reservation");
  }

  if (addr != info.base) {
    return lp_error(lp, ZX_ERR_BAD_STATE, "zx_vmar_allocate gave wrong address?!?");
  }

  return ZX_OK;
}

// Consumes 'vmo' on success, not on failure.
static zx_status_t handle_interp(launchpad_t* lp, zx_handle_t vmo, const char* interp,
                                 size_t interp_len) {
  zx_status_t status = setup_loader_svc(lp);
  if (status != ZX_OK)
    return status;

  zx_handle_t interp_vmo;
  status = loader_svc_rpc(lp->special_handles[HND_LDSVC_LOADER], LDMSG_OP_LOAD_OBJECT, interp,
                          interp_len, &interp_vmo);
  if (status != ZX_OK)
    return status;

  if (lp->fresh_process) {
    // A fresh process using PT_INTERP might be loading a libc.so that
    // supports sanitizers, so in that case (the most common case)
    // keep the mappings launchpad makes out of the low address region.
    status = reserve_low_address_space(lp);
    if (status != ZX_OK)
      return status;
  }

  elf_load_info_t* elf;
  zx_handle_t segments_vmar;
  status = elf_load_start(interp_vmo, NULL, 0, &elf);
  if (status == ZX_OK) {
    status = elf_load_finish(lp_vmar(lp), elf, interp_vmo, &segments_vmar, &lp->base, &lp->entry);
    elf_load_destroy(elf);
  }
  zx_handle_close(interp_vmo);

  if (status == ZX_OK) {
    if (lp->special_handles[HND_EXEC_VMO] != ZX_HANDLE_INVALID)
      zx_handle_close(lp->special_handles[HND_EXEC_VMO]);
    lp->special_handles[HND_EXEC_VMO] = vmo;
    if (lp->special_handles[HND_SEGMENTS_VMAR] != ZX_HANDLE_INVALID)
      zx_handle_close(lp->special_handles[HND_SEGMENTS_VMAR]);
    lp->special_handles[HND_SEGMENTS_VMAR] = segments_vmar;
    lp->loader_message = true;
  }

  return status;
}

static zx_status_t launchpad_elf_load_body(launchpad_t* lp, const char* hdr_buf, size_t buf_sz,
                                           zx_handle_t vmo) {
  elf_load_info_t* elf;
  zx_status_t status;

  if (lp->error)
    goto done;
  if ((status = elf_load_start(vmo, hdr_buf, buf_sz, &elf)) != ZX_OK) {
    lp_error(lp, status, "elf_load: elf_load_start() failed");
  } else {
    char* interp;
    size_t interp_len;
    status = elf_load_get_interp(elf, vmo, &interp, &interp_len);
    if (status != ZX_OK) {
      lp_error(lp, status, "elf_load: get_interp() failed");
    } else {
      if (interp == NULL) {
        zx_handle_t segments_vmar;
        status = elf_load_finish(lp_vmar(lp), elf, vmo, &segments_vmar, &lp->base, &lp->entry);
        if (status != ZX_OK) {
          lp_error(lp, status, "elf_load: elf_load_finish() failed");
        } else {
          // With no PT_INTERP, we obey PT_GNU_STACK.p_memsz for
          // the stack size setting.  With PT_INTERP, the dynamic
          // linker is responsible for that.
          check_elf_stack_size(lp, elf);
          lp->loader_message = false;
          launchpad_add_handle(lp, segments_vmar, PA_HND(PA_VMAR_LOADED, 0));
        }
      } else {
        if ((status = handle_interp(lp, vmo, interp, interp_len))) {
          lp_error(lp, status, "elf_load: handle_interp failed");
        } else {
          // handle_interp() takes ownership of vmo on success
          vmo = ZX_HANDLE_INVALID;
        }
        free(interp);
      }
    }
    elf_load_destroy(elf);
  }
done:
  if (vmo)
    zx_handle_close(vmo);
  return lp->error;
}

__EXPORT
zx_status_t launchpad_elf_load(launchpad_t* lp, zx_handle_t vmo) {
  if (vmo == ZX_HANDLE_INVALID)
    return lp_error(lp, ZX_ERR_INVALID_ARGS, "elf_load: invalid vmo");

  return launchpad_elf_load_body(lp, NULL, 0, vmo);
}

static zx_handle_t vdso_vmo = ZX_HANDLE_INVALID;
static mtx_t vdso_mutex = MTX_INIT;
static void vdso_lock(void) __TA_ACQUIRE(&vdso_mutex) { mtx_lock(&vdso_mutex); }
static void vdso_unlock(void) __TA_RELEASE(&vdso_mutex) { mtx_unlock(&vdso_mutex); }
static zx_handle_t vdso_get_vmo(void) {
  if (vdso_vmo == ZX_HANDLE_INVALID)
    vdso_vmo = zx_take_startup_handle(PA_HND(PA_VMO_VDSO, 0));
  return vdso_vmo;
}

__EXPORT
zx_status_t launchpad_get_vdso_vmo(zx_handle_t* out) {
  vdso_lock();
  zx_status_t status = zx_handle_duplicate(vdso_get_vmo(), ZX_RIGHT_SAME_RIGHTS, out);
  vdso_unlock();
  return status;
}

__EXPORT
zx_handle_t launchpad_set_vdso_vmo(zx_handle_t new_vdso_vmo) {
  vdso_lock();
  zx_handle_t old = vdso_vmo;
  vdso_vmo = new_vdso_vmo;
  vdso_unlock();
  return old;
}

__EXPORT
zx_status_t launchpad_add_vdso_vmo(launchpad_t* lp) {
  if (lp->error)
    return lp->error;
  zx_handle_t vdso;
  zx_status_t status;
  if ((status = launchpad_get_vdso_vmo(&vdso)) != ZX_OK)
    return lp_error(lp, status, "add_vdso_vmo: get_vdso_vmo failed");
  // Takes ownership of 'vdso'.
  return launchpad_add_handle(lp, vdso, PA_HND(PA_VMO_VDSO, 0));
}

__EXPORT
zx_status_t launchpad_load_vdso(launchpad_t* lp, zx_handle_t vmo) {
  if (vmo != ZX_HANDLE_INVALID)
    return launchpad_elf_load_extra(lp, vmo, &lp->vdso_base, NULL);
  vdso_lock();
  vmo = vdso_get_vmo();
  zx_status_t status = launchpad_elf_load_extra(lp, vmo, &lp->vdso_base, NULL);
  vdso_unlock();
  return status;
}

__EXPORT
zx_status_t launchpad_get_entry_address(launchpad_t* lp, zx_vaddr_t* entry) {
  if (lp->entry == 0)
    return ZX_ERR_BAD_STATE;
  *entry = lp->entry;
  return ZX_OK;
}

__EXPORT
zx_status_t launchpad_get_base_address(launchpad_t* lp, zx_vaddr_t* base) {
  if (lp->base == 0)
    return ZX_ERR_BAD_STATE;
  *base = lp->base;
  return ZX_OK;
}

__EXPORT
bool launchpad_send_loader_message(launchpad_t* lp, bool do_send) {
  bool result = lp->loader_message;
  if (!lp->error)
    lp->loader_message = do_send;
  return result;
}

__EXPORT
zx_handle_t launchpad_use_loader_service(launchpad_t* lp, zx_handle_t svc) {
  zx_handle_t result = lp->special_handles[HND_LDSVC_LOADER];
  lp->special_handles[HND_LDSVC_LOADER] = svc;
  return result;
}

// Construct a load message. Fill in the header, args, and environment
// fields, and leave space for the handles, which should be filled in
// by the caller.
// TODO(mcgrathr): One day we'll have a gather variant of message_write
// and then we can send this without copying into a temporary buffer.
static zx_status_t build_message(launchpad_t* lp, size_t num_handles, void** msg_buf,
                                 size_t* buf_size, bool with_names) {
  size_t msg_size = sizeof(zx_proc_args_t);
  static_assert(sizeof(zx_proc_args_t) % sizeof(uint32_t) == 0,
                "handles misaligned in load message");
  msg_size += sizeof(uint32_t) * num_handles;
  msg_size += lp->args_len;
  msg_size += lp->env_len;
  msg_size += lp->names_len;
  void* msg = malloc(msg_size);
  if (msg == NULL)
    return ZX_ERR_NO_MEMORY;

  zx_proc_args_t* header = msg;

  memset(header, 0, sizeof(*header));
  header->protocol = ZX_PROCARGS_PROTOCOL;
  header->version = ZX_PROCARGS_VERSION;
  header->handle_info_off = sizeof(*header);

  // Include the argument strings so the dynamic linker can use argv[0]
  // in messages it prints.
  header->args_off = header->handle_info_off + sizeof(uint32_t) * num_handles;
  header->args_num = lp->argc;
  if (header->args_num > 0) {
    uint8_t* args_start = (uint8_t*)msg + header->args_off;
    memcpy(args_start, lp->args, lp->args_len);
  }

  // Include the environment strings so the dynamic linker can
  // see options like LD_DEBUG or whatnot.
  if (lp->envc > 0) {
    header->environ_off = header->args_off + lp->args_len;
    header->environ_num = lp->envc;
    uint8_t* env_start = (uint8_t*)msg + header->environ_off;
    memcpy(env_start, lp->env, lp->env_len);
  }

  if (with_names && (lp->namec > 0)) {
    header->names_off = header->args_off + lp->args_len + lp->env_len;
    header->names_num = lp->namec;
    uint8_t* names_start = (uint8_t*)msg + header->names_off;
    memcpy(names_start, lp->names, lp->names_len);
  }

  *msg_buf = msg;
  *buf_size = msg_size;
  return ZX_OK;
}

static zx_status_t send_loader_message(launchpad_t* lp, zx_handle_t first_thread,
                                       zx_handle_t tochannel) {
  void* msg;
  size_t msg_size;
  size_t num_handles = HND_SPECIAL_COUNT + HND_LOADER_COUNT;

  zx_status_t status = build_message(lp, num_handles, &msg, &msg_size, false);
  if (status != ZX_OK)
    return status;

  zx_proc_args_t* header = msg;
  uint32_t* msg_handle_info;
  msg_handle_info = (uint32_t*)((uint8_t*)msg + header->handle_info_off);

  // This loop should be completely unrolled.  But using a switch here
  // gives us compiler warnings if we forget to handle any of the special
  // types listed in the enum.
  zx_handle_t handles[HND_SPECIAL_COUNT + HND_LOADER_COUNT];
  size_t nhandles = 0;
  for (enum special_handles i = 0; i <= HND_SPECIAL_COUNT; ++i) {
    uint32_t id = 0;  // -Wall
    switch (i) {
      case HND_SPECIAL_COUNT:;
        // Duplicate the handles for the loader so we can send them in the
        // loader message and still have them later.
        zx_handle_t proc;
        status = zx_handle_duplicate(lp_proc(lp), ZX_RIGHT_SAME_RIGHTS, &proc);
        if (status != ZX_OK) {
          free(msg);
          return status;
        }
        zx_handle_t vmar;
        status = zx_handle_duplicate(lp_vmar(lp), ZX_RIGHT_SAME_RIGHTS, &vmar);
        if (status != ZX_OK) {
          zx_handle_close(proc);
          free(msg);
          return status;
        }
        zx_handle_t thread;
        status = zx_handle_duplicate(first_thread, ZX_RIGHT_SAME_RIGHTS, &thread);
        if (status != ZX_OK) {
          zx_handle_close(proc);
          zx_handle_close(vmar);
          free(msg);
          return status;
        }
        handles[nhandles] = proc;
        msg_handle_info[nhandles] = PA_PROC_SELF;
        handles[nhandles + 1] = vmar;
        msg_handle_info[nhandles + 1] = PA_VMAR_ROOT;
        handles[nhandles + 2] = thread;
        msg_handle_info[nhandles + 2] = PA_THREAD_SELF;
        nhandles += HND_LOADER_COUNT;
        continue;

      case HND_LDSVC_LOADER:
        id = PA_LDSVC_LOADER;
        break;

      case HND_EXEC_VMO:
        id = PA_VMO_EXECUTABLE;
        break;

      case HND_SEGMENTS_VMAR:
        id = PA_VMAR_LOADED;
        break;
    }
    if (lp->special_handles[i] != ZX_HANDLE_INVALID) {
      handles[nhandles] = lp->special_handles[i];
      msg_handle_info[nhandles] = id;
      ++nhandles;
    }
  }

  status = zx_channel_write(tochannel, 0, msg, msg_size, handles, nhandles);
  if (status == ZX_OK)
    lp->loader_message = false;

  // message_write consumed all those handles.
  for (enum special_handles i = 0; i < HND_SPECIAL_COUNT; ++i)
    lp->special_handles[i] = ZX_HANDLE_INVALID;

  free(msg);
  return status;
}

__EXPORT
size_t launchpad_set_stack_size(launchpad_t* lp, size_t new_size) {
  size_t old_size = lp->stack_size;
  if (new_size >= (SIZE_MAX & -PAGE_SIZE)) {
    // Ridiculously large size won't actually work at allocation time,
    // but at least page rounding won't wrap it around to zero.
    new_size = SIZE_MAX & -PAGE_SIZE;
  } else if (new_size > 0) {
    // Round up to page size.
    new_size = (new_size + PAGE_SIZE - 1) & -PAGE_SIZE;
  }
  if (lp->error == ZX_OK) {
    lp->stack_size = new_size;
    lp->set_stack_size = true;
  }
  return old_size;
}

static zx_status_t prepare_start(launchpad_t* lp, launchpad_start_data_t* result) {
  if (lp->entry == 0)
    return lp_error(lp, ZX_ERR_BAD_STATE, "prepare start bad state");

  zx_status_t status = ZX_OK;
  zx_handle_t to_child = ZX_HANDLE_INVALID;
  zx_handle_t bootstrap = ZX_HANDLE_INVALID;
  zx_handle_t process = ZX_HANDLE_INVALID;
  zx_handle_t root_vmar = ZX_HANDLE_INVALID;
  zx_handle_t thread = ZX_HANDLE_INVALID;
  void* msg = NULL;

  status = zx_channel_create(0, &to_child, &bootstrap);
  if (status != ZX_OK)
    return lp_error(lp, status, "start: cannot create channel");

  const char* thread_name = "initial-thread";
  status = zx_thread_create(lp_proc(lp), thread_name, strlen(thread_name), 0, &thread);
  if (status != ZX_OK) {
    lp_error(lp, status, "cannot create initial thread");
    goto cleanup;
  }

  // Pass the thread handle down to the child.  The handle we pass
  // will be consumed by message_write.  So we need a duplicate to
  // pass to zx_process_start later.
  zx_handle_t thread_copy;
  status = zx_handle_duplicate(thread, ZX_RIGHT_SAME_RIGHTS, &thread_copy);
  if (status != ZX_OK) {
    lp_error(lp, status, "cannot duplicate thread handle");
    goto cleanup;
  }

  status = launchpad_add_handle(lp, thread_copy, PA_THREAD_SELF);
  if (status != ZX_OK) {
    lp_error(lp, status, "cannot add thread self handle");
    goto cleanup;
  }

  bool sent_loader_message = lp->loader_message;
  if (lp->loader_message) {
    status = send_loader_message(lp, thread, to_child);
    if (status != ZX_OK) {
      lp_error(lp, status, "failed to send loader message");
      goto cleanup;
    }
  }

  bool allocate_stack = !lp->set_stack_size || lp->stack_size > 0;

  size_t size;
  if (build_message(lp, lp->handle_count + (allocate_stack ? 1 : 0), &msg, &size, true) != ZX_OK) {
    lp_error(lp, ZX_ERR_NO_MEMORY, "out of memory assembling procargs message");
    goto cleanup;
  }
  zx_proc_args_t* header = msg;
  uint32_t* next_handle = mempcpy((uint8_t*)msg + header->handle_info_off, lp->handles_info,
                                  lp->handle_count * sizeof(lp->handles_info[0]));
  if (allocate_stack)
    *next_handle = PA_VMO_STACK;

  // Figure out how big an initial thread to allocate.
  char stack_vmo_name[ZX_MAX_NAME_LEN];
  size_t stack_size;
  if (sent_loader_message && !lp->set_stack_size) {
    // The initial stack will be used just for startup work and to
    // contain the bootstrap message.  Make it only as big as needed:
    // the message itself and its array of handles, plus some slop.
    stack_size = size + (lp->handle_count * sizeof(zx_handle_t));

    // This constant is defined by the C library in <limits.h>.  It's
    // tuned to be enough to cover the dynamic linker and C library
    // startup code's stack usage (up until the point it switches to
    // its own stack in __libc_start_main), but leave a little space so
    // for small bootstrap message sizes the stack needs only one page.
    stack_size += PTHREAD_STACK_MIN;
    stack_size = (stack_size + PAGE_SIZE - 1) & -PAGE_SIZE;

    snprintf(stack_vmo_name, sizeof(stack_vmo_name), "stack: msg of %#zx", size);
  } else {
    // Use the requested or default size.
    stack_size = lp->set_stack_size ? lp->stack_size : ZIRCON_DEFAULT_STACK_SIZE;
    snprintf(stack_vmo_name, sizeof(stack_vmo_name), "stack: %s %#zx",
             lp->set_stack_size ? "explicit" : "default", stack_size);

    // Assume the process will read the bootstrap message onto its
    // initial thread's stack.  If it would need more than half its
    // stack just to read the message, consider that an unreasonably
    // large size for the message (presumably arguments and
    // environment strings that are unreasonably large).
    if (stack_size > 0 && size > stack_size / 2) {
      lp_error(lp, ZX_ERR_BUFFER_TOO_SMALL, "procargs message is too large");
      goto cleanup;
    }
  }

  zx_vaddr_t sp = 0;
  if (stack_size > 0) {
    // Allocate the initial thread's stack.
    zx_handle_t stack_vmo;
    zx_status_t status = zx_vmo_create(stack_size, 0, &stack_vmo);
    if (status != ZX_OK) {
      lp_error(lp, status, "cannot create stack vmo");
      goto cleanup;
    }
    zx_object_set_property(stack_vmo, ZX_PROP_NAME, stack_vmo_name, strlen(stack_vmo_name));
    zx_vaddr_t stack_base;
    status = zx_vmar_map(lp_vmar(lp), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0, stack_vmo, 0,
                         stack_size, &stack_base);
    if (status != ZX_OK) {
      zx_handle_close(stack_vmo);
      lp_error(lp, status, "cannot map stack vmo");
      goto cleanup;
    }

    ZX_DEBUG_ASSERT(stack_size % PAGE_SIZE == 0);
    sp = compute_initial_stack_pointer(stack_base, stack_size);
    // Pass the stack VMO to the process.  Our protocol with the
    // new process is that we warrant that this is the VMO from
    // which the initial stack is mapped and that we've exactly
    // mapped the entire thing, so vm_object_get_size on this in
    // concert with the initial SP value tells it the exact bounds
    // of its stack.
    //
    // Note this expands the handle list after we've already
    // built the bootstrap message.  We shoved an extra info
    // slot with PA_VMO_STACK into the message, so now this new
    // final handle will correspond to that slot.
    status = launchpad_add_handle(lp, stack_vmo, PA_VMO_STACK);
    if (status != ZX_OK) {
      // launchpad_add_handle consumed the handle even in the error case.
      goto cleanup;
    }
  }

  if (lp->reserve_vmar != ZX_HANDLE_INVALID) {
    // We're done doing mappings, so clear out the reservation VMAR.
    status = zx_vmar_destroy(lp->reserve_vmar);
    if (status != ZX_OK) {
      lp_error(lp, status,
               "\
zx_vmar_destroy failed on low address space reservation VMAR");
      goto cleanup;
    }
    status = zx_handle_close(lp->reserve_vmar);
    if (status != ZX_OK) {
      lp_error(lp, status,
               "\
zx_handle_close failed on low address space reservation VMAR");
      goto cleanup;
    }
    lp->reserve_vmar = ZX_HANDLE_INVALID;
  }

  // The process handle in lp->handles[0] will be consumed by message_write.
  // So we'll need a duplicate to do process operations later.
  status = zx_handle_duplicate(lp_proc(lp), ZX_RIGHT_SAME_RIGHTS, &process);
  if (status != ZX_OK) {
    lp_error(lp, status, "cannot duplicate process handle");
    goto cleanup;
  }

  // The root_vmar handle in lp->handles[0] will be consumed by message_write.
  // So we'll need a duplicate to do process operations later.
  status = zx_handle_duplicate(lp_vmar(lp), ZX_RIGHT_SAME_RIGHTS, &root_vmar);
  if (status != ZX_OK) {
    lp_error(lp, status, "cannot duplicate root vmar handle");
    goto cleanup;
  }

  status = zx_channel_write(to_child, 0, msg, size, lp->handles, lp->handle_count);

  // message_write consumed all the handles.
  for (size_t i = 0; i < lp->handle_count; ++i)
    lp->handles[i] = ZX_HANDLE_INVALID;
  lp->handle_count = 0;

  if (status != ZX_OK) {
    lp_error(lp, status, "failed to write procargs message");
    goto cleanup;
  }

  zx_handle_close(to_child);
  free(msg);

  result->process = process;
  result->root_vmar = root_vmar;
  result->thread = thread;
  result->entry = lp->entry;
  result->stack = sp;
  result->bootstrap = bootstrap;
  result->vdso_base = lp->vdso_base;
  result->base = lp->base;
  return ZX_OK;

cleanup:
  if (to_child != ZX_HANDLE_INVALID)
    zx_handle_close(to_child);
  if (bootstrap != ZX_HANDLE_INVALID)
    zx_handle_close(bootstrap);
  if (process != ZX_HANDLE_INVALID)
    zx_handle_close(process);
  if (root_vmar != ZX_HANDLE_INVALID)
    zx_handle_close(root_vmar);
  if (thread != ZX_HANDLE_INVALID)
    zx_handle_close(thread);
  free(msg);
  return lp->error;
}

// Start the process running.  If the send_loader_message flag is
// set and this succeeds in sending the initial bootstrap message,
// it clears the loader-service handle.  If this succeeds in sending
// the main bootstrap message, it clears the list of handles to
// transfer (after they've been transferred) as well as the process
// handle.
//
// Returns the process handle via |process_out| on success, giving
// ownership to the caller.  On failure, the return value doesn't
// distinguish failure to send the first or second message from
// failure to start the process, so on failure the loader-service
// handle might or might not have been cleared and the handles to
// transfer might or might not have been cleared.
static zx_status_t launchpad_start(launchpad_t* lp, zx_handle_t* process_out) {
  if (lp->error)
    return lp->error;

  launchpad_start_data_t data;
  zx_status_t status = prepare_start(lp, &data);
  if (status != ZX_OK)
    return status;

  status = zx_process_start(data.process, data.thread, data.entry, data.stack, data.bootstrap,
                            data.vdso_base);

  zx_handle_close(data.thread);
  zx_handle_close(data.root_vmar);

  if (status != ZX_OK) {
    zx_handle_close(data.process);
    return lp_error(lp, status, "zx_process_start() failed");
  }

  *process_out = data.process;
  return ZX_OK;
}

__EXPORT
zx_status_t launchpad_go(launchpad_t* lp, zx_handle_t* proc, const char** errmsg) {
  zx_handle_t h = ZX_HANDLE_INVALID;
  zx_status_t status = launchpad_start(lp, &h);
  if (errmsg)
    *errmsg = lp->errmsg;
  if (status == ZX_OK) {
    if (proc) {
      *proc = h;
    } else {
      zx_handle_close(h);
    }
  }
  launchpad_destroy(lp);
  return status;
}

__EXPORT
zx_status_t launchpad_ready_set(launchpad_t* lp, launchpad_start_data_t* data,
                                const char** errmsg) {
  zx_status_t status = prepare_start(lp, data);
  if (errmsg)
    *errmsg = lp->errmsg;
  launchpad_destroy(lp);
  return status;
}

__EXPORT
zx_status_t launchpad_load_from_vmo(launchpad_t* lp, zx_handle_t vmo) {
  launchpad_elf_load(lp, vmo);
  launchpad_load_vdso(lp, ZX_HANDLE_INVALID);
  return launchpad_add_vdso_vmo(lp);
}

__EXPORT
zx_status_t launchpad_load_from_file(launchpad_t* lp, const char* path) {
  zx_handle_t vmo;
  zx_status_t status = launchpad_vmo_from_file(path, &vmo);
  if (status == ZX_OK) {
    return launchpad_load_from_vmo(lp, vmo);
  } else {
    return lp_error(lp, status, "launchpad_vmo_from_file failure");
  }
}
