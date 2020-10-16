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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <zircon/syscalls/policy.h>

#include <fbl/ref_ptr.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>
#include <object/socket_dispatcher.h>

#include "priv.h"

#define LOCAL_TRACE 0

// zx_status_t zx_socket_create
zx_status_t sys_socket_create(uint32_t options, user_out_handle* out0, user_out_handle* out1) {
  auto up = ProcessDispatcher::GetCurrent();
  zx_status_t res = up->EnforceBasicPolicy(ZX_POL_NEW_SOCKET);
  if (res != ZX_OK)
    return res;

  KernelHandle<SocketDispatcher> handle0, handle1;
  zx_rights_t rights;
  zx_status_t result = SocketDispatcher::Create(options, &handle0, &handle1, &rights);

  if (result == ZX_OK)
    result = out0->make(ktl::move(handle0), rights);
  if (result == ZX_OK)
    result = out1->make(ktl::move(handle1), rights);
  return result;
}

// zx_status_t zx_socket_write
zx_status_t sys_socket_write(zx_handle_t handle, uint32_t options, user_in_ptr<const void> buffer,
                             size_t size, user_out_ptr<size_t> actual) {
  LTRACEF("handle %x\n", handle);

  if ((size > 0u) && !buffer)
    return ZX_ERR_INVALID_ARGS;

  if (options)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<SocketDispatcher> socket;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &socket);
  if (status != ZX_OK)
    return status;

  size_t nwritten;
  status = socket->Write(buffer.reinterpret<const char>(), size, &nwritten);

  // Caller may ignore results if desired.
  if (status == ZX_OK && actual)
    status = actual.copy_to_user(nwritten);

  return status;
}

// zx_status_t zx_socket_read
zx_status_t sys_socket_read(zx_handle_t handle, uint32_t options, user_out_ptr<void> buffer,
                            size_t size, user_out_ptr<size_t> actual) {
  LTRACEF("handle %x\n", handle);

  if (!buffer && size > 0)
    return ZX_ERR_INVALID_ARGS;

  if (options & ~ZX_SOCKET_PEEK)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<SocketDispatcher> socket;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_READ, &socket);
  if (status != ZX_OK)
    return status;

  SocketDispatcher::ReadType type = (options & ZX_SOCKET_PEEK)
                                        ? SocketDispatcher::ReadType::kPeek
                                        : SocketDispatcher::ReadType::kConsume;

  size_t nread;
  status = socket->Read(type, buffer.reinterpret<char>(), size, &nread);

  // Caller may ignore results if desired.
  if (status == ZX_OK && actual)
    status = actual.copy_to_user(nread);

  return status;
}

// zx_status_t zx_socket_shutdown
zx_status_t sys_socket_shutdown(zx_handle_t handle, uint32_t options) {
  if (options & ~ZX_SOCKET_SHUTDOWN_MASK)
    return ZX_ERR_INVALID_ARGS;

  auto up = ProcessDispatcher::GetCurrent();

  fbl::RefPtr<SocketDispatcher> socket;
  zx_status_t status = up->handle_table().GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &socket);
  if (status != ZX_OK)
    return status;

  return socket->Shutdown(options & ZX_SOCKET_SHUTDOWN_MASK);
}
