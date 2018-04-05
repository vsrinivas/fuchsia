// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <platform.h>
#include <stdint.h>
#include <stdlib.h>
#include <trace.h>

#include <lib/user_copy/user_ptr.h>
#include <object/fifo_dispatcher.h>
#include <object/handle.h>
#include <object/process_dispatcher.h>

#include <zircon/syscalls/policy.h>
#include <fbl/ref_ptr.h>

#include "priv.h"

#define LOCAL_TRACE 0

zx_status_t sys_fifo_create(uint32_t count, uint32_t elemsize, uint32_t options,
                            user_out_handle* out0, user_out_handle* out1) {
    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryPolicy(ZX_POL_NEW_FIFO);
    if (res != ZX_OK)
        return res;

    fbl::RefPtr<Dispatcher> dispatcher0;
    fbl::RefPtr<Dispatcher> dispatcher1;
    zx_rights_t rights;
    zx_status_t result = FifoDispatcher::Create(count, elemsize, options,
                                                &dispatcher0, &dispatcher1, &rights);

    if (result == ZX_OK)
        result = out0->make(fbl::move(dispatcher0), rights);
    if (result == ZX_OK)
        result = out1->make(fbl::move(dispatcher1), rights);
    return result;
}

zx_status_t sys_fifo_write(zx_handle_t handle, user_in_ptr<const void> entries,
                           size_t len, user_out_ptr<uint32_t> actual_out) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<FifoDispatcher> fifo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_WRITE, &fifo);
    if (status != ZX_OK)
        return status;

    uint32_t actual;
    status = fifo->WriteFromUser(entries.reinterpret<const uint8_t>(), len, &actual);
    if (status != ZX_OK)
        return status;

    status = actual_out.copy_to_user(actual);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}

zx_status_t sys_fifo_write_old(zx_handle_t handle, user_in_ptr<const void> entries,
                               size_t len, user_out_ptr<uint32_t> actual_out) {
    return sys_fifo_write(handle, entries, len, actual_out);
}

zx_status_t sys_fifo_read(zx_handle_t handle, user_out_ptr<void> entries, size_t len,
                          user_out_ptr<uint32_t> actual_out) {
    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<FifoDispatcher> fifo;
    zx_status_t status = up->GetDispatcherWithRights(handle, ZX_RIGHT_READ, &fifo);
    if (status != ZX_OK)
        return status;

    uint32_t actual;
    status = fifo->ReadToUser(entries.reinterpret<uint8_t>(), len, &actual);
    if (status != ZX_OK)
        return status;

    status = actual_out.copy_to_user(actual);
    if (status != ZX_OK)
        return status;

    return ZX_OK;
}

zx_status_t sys_fifo_read_old(zx_handle_t handle, user_out_ptr<void> entries, size_t len,
                              user_out_ptr<uint32_t> actual_out) {
    return sys_fifo_read(handle, entries, len, actual_out);
}
