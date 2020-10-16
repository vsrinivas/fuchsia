// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <lib/user_copy/user_ptr.h>
#include <platform.h>
#include <stdint.h>
#include <stdlib.h>
#include <trace.h>
#include <zircon/syscalls/policy.h>

#include <fbl/ref_ptr.h>
#include <object/fifo_dispatcher.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>

#include "priv.h"

#define LOCAL_TRACE 0

// zx_status_t zx_fifo_create
zx_status_t sys_fifo_create(size_t count, size_t elemsize, uint32_t options, user_out_handle* out0,
                            user_out_handle* out1) {
  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t res = up->EnforceBasicPolicy(ZX_POL_NEW_FIFO);
  if (res != ZX_OK)
    return res;

  KernelHandle<FifoDispatcher> handle0;
  KernelHandle<FifoDispatcher> handle1;
  zx_rights_t rights;
  zx_status_t result =
      FifoDispatcher::Create(count, elemsize, options, &handle0, &handle1, &rights);

  if (result == ZX_OK)
    result = out0->make(ktl::move(handle0), rights);
  if (result == ZX_OK)
    result = out1->make(ktl::move(handle1), rights);
  return result;
}

// zx_status_t zx_fifo_write
zx_status_t sys_fifo_write(zx_handle_t handle, size_t elem_size, user_in_ptr<const void> data,
                           size_t count, user_out_ptr<size_t> actual_count) {
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<FifoDispatcher> fifo;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &fifo);
  if (status != ZX_OK)
    return status;

  size_t actual;
  status = fifo->WriteFromUser(elem_size, data.reinterpret<const uint8_t>(), count, &actual);
  if (status != ZX_OK)
    return status;

  if (actual_count) {
    status = actual_count.copy_to_user(actual);
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}

// zx_status_t zx_fifo_read
zx_status_t sys_fifo_read(zx_handle_t handle, size_t elem_size, user_out_ptr<void> data,
                          size_t count, user_out_ptr<size_t> actual_count) {
  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<FifoDispatcher> fifo;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_READ, &fifo);
  if (status != ZX_OK)
    return status;

  size_t actual;
  status = fifo->ReadToUser(elem_size, data.reinterpret<uint8_t>(), count, &actual);
  if (status != ZX_OK)
    return status;

  if (actual_count) {
    status = actual_count.copy_to_user(actual);
    if (status != ZX_OK)
      return status;
  }
  return ZX_OK;
}
