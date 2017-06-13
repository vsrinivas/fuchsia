/* libunwind - a platform-independent unwind library
   Copyright 2016 The Fuchsia Authors. All rights reserved.

This file is part of libunwind.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.  */

// This merges everything into one file to keep it all together.
// There's no need for one file per function here.

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <magenta/syscalls.h>
#include <magenta/syscalls/debug.h>

#include "fuchsia.h"
#include "fuchsia_i.h"
#include "libunwind_i.h"

HIDDEN int
tdep_get_elf_image (struct elf_image *ei, pid_t pid, unw_word_t ip,
                    unsigned long *segbase, unsigned long *mapoff,
                    char *path, size_t pathlen)
{
  // This maps in the whole image, which we don't need.
  int rc = -1;

  return rc;
}

static mx_status_t
read_mem (mx_handle_t h, mx_vaddr_t vaddr, void* ptr, size_t len)
{
  size_t actual;
  mx_status_t status = mx_process_read_memory (h, vaddr, ptr, len, &actual);
  if (status < 0)
  {
    Debug (3, "read_mem @0x%" PRIxPTR " FAILED %d\n", vaddr, status);
    return status;
  }
  if (len != actual)
  {
    // TODO: Use %zd when MG-164 is fixed.
    Debug (3, "read_mem @0x%" PRIxPTR " FAILED, short read %zd\n", vaddr, actual);
    return MX_ERR_IO;
  }
  return MX_OK;
}

static mx_status_t
get_inferior_greg_buf_size (mx_handle_t thread, uint32_t* regset_size)
{
  // The general regs are defined to be in regset zero.
  mx_status_t status = mx_thread_read_state (thread, MX_THREAD_STATE_REGSET0,
                                             NULL, 0, regset_size);
  assert (status != MX_OK);
  if (status == MX_ERR_BUFFER_TOO_SMALL)
    status = MX_OK;
  return status;
}

static mx_status_t
read_inferior_gregs (mx_handle_t thread, void* buf, size_t regset_size)
{
  uint32_t buf_size = (uint32_t) regset_size;
  // By convention the general regs are in regset 0.
  mx_status_t status = mx_thread_read_state (thread, MX_THREAD_STATE_REGSET0, buf, buf_size, &buf_size);
  return status;
}

static uint32_t
get_uint32 (void* buf)
{
  uint32_t value = 0;
  memcpy (&value, buf, sizeof (value));
  return value;
}

static uint64_t
get_uint64 (void* buf)
{
  uint64_t value = 0;
  memcpy (&value, buf, sizeof (value));
  return value;
}

// Subroutine of remote_find_proc_info to simplify it.

static int
get_unwind_info (unw_fuchsia_info_t* cxt, unw_addr_space_t as, unw_word_t ip)
{
  struct as_elf_dyn_info* edi = &cxt->edi;
  unsigned long segbase, mapoff;

  // Can we use the previously found info?
  if ((edi->di_cache.format != -1
       && ip >= edi->di_cache.start_ip && ip < edi->di_cache.end_ip)
      || (edi->di_debug.format != -1
          && ip >= edi->di_debug.start_ip && ip < edi->di_debug.end_ip))
    return 0;

  unwi_invalidate_as_edi(edi);
  edi->arg = cxt;

  unw_word_t base;
  const char* name;
  if (!cxt->lookup_dso (cxt->dsos, ip, &base, &name))
  {
    Debug (3, "pc 0x%lx not in any dso\n", (long) ip);
    return -UNW_ENOINFO;
  }
  Debug (3, "pc 0x%lx in dso %s, base 0x%lx\n", (long) ip, name, (long) base);

  segbase = base;
  mapoff = 0;

  int ret = dwarf_as_find_unwind_table (edi, as, name, segbase, mapoff, ip);
  Debug (3, "dwarf_as_find_unwind_table returned %d\n", ret);
  if (ret < 0)
    return -UNW_ENOINFO;

  /* This can happen in corner cases where dynamically generated
     code falls into the same page that contains the data-segment
     and the page-offset of the code is within the first page of
     the executable.  */
  if (edi->di_cache.format != -1
      && (ip < edi->di_cache.start_ip || ip >= edi->di_cache.end_ip))
    edi->di_cache.format = -1;

  if (edi->di_debug.format != -1
      && (ip < edi->di_debug.start_ip || ip >= edi->di_debug.end_ip))
    edi->di_debug.format = -1;

  if (edi->di_cache.format == -1
      && edi->di_debug.format == -1)
    return -UNW_ENOINFO;

  return 0;
}

static int
remote_find_proc_info (unw_addr_space_t as, unw_word_t ip, unw_proc_info_t *pi,
                       int need_unwind_info, void *arg)
{
  Debug (3, "called, as %p, ip 0x%llx, need_unwind_info %d\n", as, (long long) ip, need_unwind_info);

  unw_fuchsia_info_t* cxt = arg;
  int ret;

  ret = get_unwind_info (cxt, as, ip);
  if (ret < 0)
  {
    Debug (3, "get_unwind_info failed: %d\n", ret);
    return -UNW_ENOINFO;
  }

  ret = -UNW_ENOINFO;
  if (ret == -UNW_ENOINFO && cxt->edi.di_cache.format != -1)
    ret = dwarf_search_unwind_table (as, ip, &cxt->edi.di_cache, pi,
                                     need_unwind_info, arg);
  if (ret == -UNW_ENOINFO && cxt->edi.di_debug.format != -1)
    ret = dwarf_search_unwind_table (as, ip, &cxt->edi.di_debug, pi,
                                     need_unwind_info, arg);

  Debug (3, "returning %d\n", ret);
  return ret;
}

static void
remote_put_unwind_info (unw_addr_space_t as, unw_proc_info_t *pi, void *arg)
{
  Debug (3, "called\n");
  // FIXME: This is what the ptrace code does, but I think this should do what
  // the dwarf put_unwind_info does. See dwarf/Gparser.c. In particular,
  // dwarf_extract_proc_info_from_fde calls mempool_alloc.
  free (pi->unwind_info);
  pi->unwind_info = NULL;
}

static int
remote_get_dyn_info_list_addr (unw_addr_space_t as, unw_word_t *dil_addr,
                               void *arg)
{
  Debug (3, "called\n");
  return -UNW_ENOINFO;
}

static void
fill_hex (char *buf, size_t buf_size, void *src, size_t size)
{
  if (buf_size < size * 3 + 1)
  {
    *buf = 0;
    return;
  }

  char *p = buf;
  unsigned char *s = (unsigned char*) src;

  while (size > 0)
  {
    sprintf (p, " %02x", *s);
    p += 3;
    ++s;
    --size;
  }
  *p = 0;
}

static int
remote_access_mem (unw_addr_space_t as, unw_word_t addr, unw_word_t *val,
                   int write, void *arg)
{
  Debug (3, "called, addr 0x%lx\n", (long) addr);
  unw_fuchsia_info_t* cxt = arg;
  mx_handle_t process = cxt->process;
  if (write)
  {
    Debug (3, "writing to mem\n");
    return -UNW_EINVAL;
  }
  mx_status_t status = read_mem (process, addr, val, sizeof(*val));
  if (status < 0)
      return -UNW_EINVAL;
  char dump[3 * 8 + 1];
  fill_hex (dump, sizeof(dump), val, sizeof(*val));
  Debug (3, "returning 0, val %s\n", dump);
  return 0;
}

static int
remote_access_raw_mem (unw_addr_space_t as, unw_word_t addr,
                       void* buf, size_t size, int write, void *arg)
{
  Debug (3, "called, addr 0x%lx, size %lu\n", (long) addr, (long) size);
  unw_fuchsia_info_t* cxt = arg;
  mx_handle_t process = cxt->process;
  if (write)
  {
    Debug (3, "writing to mem\n");
    return -UNW_EINVAL;
  }
  mx_status_t status = read_mem (process, addr, buf, size);
  if (status < 0)
  {
    Debug (3, "read failed: %d\n", status);
    return -UNW_EINVAL;
  }
  char dump[3 * 8 + 1];
  size_t to_dump = 8;
  if (size < 8)
    to_dump = size;
  fill_hex (dump, sizeof(dump), buf, to_dump);
  Debug (3, "returning 0, val %s%s\n", dump, to_dump < size ? " ..." : "");
  return 0;
}

static int
remote_access_reg (unw_addr_space_t as, unw_regnum_t reg, unw_word_t *val,
                   int write, void *arg)
{
  Debug (3, "called, regno %d\n", (int) reg);
  unw_fuchsia_info_t* cxt = arg;
  mx_handle_t thread = cxt->thread;
  if (write)
  {
    Debug (3, "writing to reg\n");
    return -UNW_EINVAL;
  }
  if (!unw_is_greg (reg))
  {
    Debug (3, "bad regnum: %d\n", (int) reg);
    return -UNW_EBADREG;
  }
  uint32_t regset_size;
  mx_status_t status = get_inferior_greg_buf_size (thread, &regset_size);
  if (status != MX_OK)
  {
    Debug (3, "unable to get greg buf size: %d\n", status);
    return -UNW_EUNSPEC;
  }
  char* buf = malloc (regset_size);
  if (buf == NULL)
  {
    Debug (3, "malloc failed\n");
    return -UNW_ENOMEM;
  }
  mx_status_t r = read_inferior_gregs (thread, buf, regset_size);
  if (r < 0)
  {
    Debug (3, "error reading gregs: %d\n", r);
    free (buf);
    return -UNW_EUNSPEC;
  }
  if (sizeof(*val) == sizeof(uint32_t))
    *val = get_uint32 (buf + fuchsia_greg_offset[reg]);
  else
    *val = get_uint64 (buf + fuchsia_greg_offset[reg]);
  Debug (3, "reg val: 0x%llx\n", (long long) *val);
  free (buf);
  return 0;
}

static int
remote_access_fpreg (unw_addr_space_t as, unw_regnum_t reg, unw_fpreg_t *val,
                     int write, void *arg)
{
  Debug (3, "called\n");
  return -UNW_EBADREG;
}

static int
remote_resume (unw_addr_space_t as, unw_cursor_t *c, void *arg)
{
  Debug (3, "called\n");
  // TODO(dje): It's not clear what a good return value is here, but OTOH
  // we don't need this.
  return -UNW_EUNSPEC;
}

static int
remote_get_proc_name (unw_addr_space_t as, unw_word_t ip,
                      char *buf, size_t buf_len, unw_word_t *offp, void *arg)
{
  Debug (3, "called\n");
  return -UNW_ENOINFO;
}

const unw_accessors_t _UFuchsia_accessors =
{
  .find_proc_info         = remote_find_proc_info,
  .put_unwind_info        = remote_put_unwind_info,
  .get_dyn_info_list_addr = remote_get_dyn_info_list_addr,
  .access_mem             = remote_access_mem,
  .access_raw_mem         = remote_access_raw_mem,
  .access_reg             = remote_access_reg,
  .access_fpreg           = remote_access_fpreg,
  .resume                 = remote_resume,
  .get_proc_name          = remote_get_proc_name
};

unw_fuchsia_info_t*
unw_create_fuchsia(mx_handle_t process, mx_handle_t thread,
                   struct dsoinfo* dsos, unw_dso_lookup_func_t* lookup_dso)
{
    unw_fuchsia_info_t* result = malloc (sizeof (*result));
    if (result == NULL)
        return NULL;

    memset (result, 0, sizeof (*result));
    result->process = process;
    result->thread = thread;
    result->dsos = dsos;
    result->lookup_dso = lookup_dso;

    return result;
}

void
unw_destroy_fuchsia(unw_fuchsia_info_t* info)
{
    free (info);
}
