// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/exception.h>

namespace async {

ExceptionBase::ExceptionBase(zx_handle_t task, uint32_t options,
                             async_exception_handler_t* handler)
    : exception_{{ASYNC_STATE_INIT}, handler, task, options} {
    ZX_DEBUG_ASSERT(handler);
}

ExceptionBase::~ExceptionBase() {
    if (dispatcher_) {
        // Failure to unbind here may result in a dangling pointer...
        zx_status_t status = async_unbind_exception_port(dispatcher_, &exception_);
        ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }
}

zx_status_t ExceptionBase::Bind(async_dispatcher_t* dispatcher) {
    if (dispatcher_)
        return ZX_ERR_ALREADY_EXISTS;

    dispatcher_ = dispatcher;
    zx_status_t status = async_bind_exception_port(dispatcher, &exception_);
    if (status != ZX_OK) {
        dispatcher_ = nullptr;
    }
    return status;
}

zx_status_t ExceptionBase::Unbind() {
    if (!dispatcher_)
        return ZX_ERR_NOT_FOUND;

    async_dispatcher_t* dispatcher = dispatcher_;
    dispatcher_ = nullptr;

    zx_status_t status = async_unbind_exception_port(dispatcher, &exception_);
    // |dispatcher| is required to be single-threaded, Unbind() is
    // only supposed to be called on |dispatcher|'s thread, and
    // we verified that the port was bound before calling
    // async_unbind_exception_port().
    ZX_DEBUG_ASSERT(status != ZX_ERR_NOT_FOUND);
    return status;
}

Exception::Exception(zx_handle_t task, uint32_t options,
                     Handler handler)
    : ExceptionBase(task, options, &Exception::CallHandler),
      handler_(fbl::move(handler)) {}

Exception::~Exception() = default;

void Exception::CallHandler(async_dispatcher_t* dispatcher,
                            async_exception_t* exception,
                            zx_status_t status,
                            const zx_port_packet_t* report) {
    auto self = Dispatch<Exception>(exception);
    self->handler_(dispatcher, self, status, report);
}

} // namespace async
