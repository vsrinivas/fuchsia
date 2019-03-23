// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the banjo.examples.protocol.handle banjo file

#pragma once


#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Forward declarations
typedef void (*async_handle_handle_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_process_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_thread_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_vmo_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_channel_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_event_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_port_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_interrupt_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_debug_log_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_socket_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_resource_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_event_pair_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_job_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_vmar_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_fifo_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_guest_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_timer_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef void (*async_handle_profile_callback)(void* ctx, zx_handle_t h, zx_handle_t h2);
typedef struct async_handle_protocol async_handle_protocol_t;
typedef struct synchronous_handle_protocol synchronous_handle_protocol_t;

// Declarations
typedef struct async_handle_protocol_ops {
    void (*handle)(void* ctx, zx_handle_t h, async_handle_handle_callback callback, void* cookie);
    void (*process)(void* ctx, zx_handle_t h, async_handle_process_callback callback, void* cookie);
    void (*thread)(void* ctx, zx_handle_t h, async_handle_thread_callback callback, void* cookie);
    void (*vmo)(void* ctx, zx_handle_t h, async_handle_vmo_callback callback, void* cookie);
    void (*channel)(void* ctx, zx_handle_t h, async_handle_channel_callback callback, void* cookie);
    void (*event)(void* ctx, zx_handle_t h, async_handle_event_callback callback, void* cookie);
    void (*port)(void* ctx, zx_handle_t h, async_handle_port_callback callback, void* cookie);
    void (*interrupt)(void* ctx, zx_handle_t h, async_handle_interrupt_callback callback, void* cookie);
    void (*debug_log)(void* ctx, zx_handle_t h, async_handle_debug_log_callback callback, void* cookie);
    void (*socket)(void* ctx, zx_handle_t h, async_handle_socket_callback callback, void* cookie);
    void (*resource)(void* ctx, zx_handle_t h, async_handle_resource_callback callback, void* cookie);
    void (*event_pair)(void* ctx, zx_handle_t h, async_handle_event_pair_callback callback, void* cookie);
    void (*job)(void* ctx, zx_handle_t h, async_handle_job_callback callback, void* cookie);
    void (*vmar)(void* ctx, zx_handle_t h, async_handle_vmar_callback callback, void* cookie);
    void (*fifo)(void* ctx, zx_handle_t h, async_handle_fifo_callback callback, void* cookie);
    void (*guest)(void* ctx, zx_handle_t h, async_handle_guest_callback callback, void* cookie);
    void (*timer)(void* ctx, zx_handle_t h, async_handle_timer_callback callback, void* cookie);
    void (*profile)(void* ctx, zx_handle_t h, async_handle_profile_callback callback, void* cookie);
} async_handle_protocol_ops_t;


struct async_handle_protocol {
    async_handle_protocol_ops_t* ops;
    void* ctx;
};

static inline void async_handle_handle(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_handle_callback callback, void* cookie) {
    proto->ops->handle(proto->ctx, h, callback, cookie);
}

static inline void async_handle_process(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_process_callback callback, void* cookie) {
    proto->ops->process(proto->ctx, h, callback, cookie);
}

static inline void async_handle_thread(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_thread_callback callback, void* cookie) {
    proto->ops->thread(proto->ctx, h, callback, cookie);
}

static inline void async_handle_vmo(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_vmo_callback callback, void* cookie) {
    proto->ops->vmo(proto->ctx, h, callback, cookie);
}

static inline void async_handle_channel(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_channel_callback callback, void* cookie) {
    proto->ops->channel(proto->ctx, h, callback, cookie);
}

static inline void async_handle_event(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_event_callback callback, void* cookie) {
    proto->ops->event(proto->ctx, h, callback, cookie);
}

static inline void async_handle_port(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_port_callback callback, void* cookie) {
    proto->ops->port(proto->ctx, h, callback, cookie);
}

static inline void async_handle_interrupt(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_interrupt_callback callback, void* cookie) {
    proto->ops->interrupt(proto->ctx, h, callback, cookie);
}

static inline void async_handle_debug_log(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_debug_log_callback callback, void* cookie) {
    proto->ops->debug_log(proto->ctx, h, callback, cookie);
}

static inline void async_handle_socket(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_socket_callback callback, void* cookie) {
    proto->ops->socket(proto->ctx, h, callback, cookie);
}

static inline void async_handle_resource(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_resource_callback callback, void* cookie) {
    proto->ops->resource(proto->ctx, h, callback, cookie);
}

static inline void async_handle_event_pair(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_event_pair_callback callback, void* cookie) {
    proto->ops->event_pair(proto->ctx, h, callback, cookie);
}

static inline void async_handle_job(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_job_callback callback, void* cookie) {
    proto->ops->job(proto->ctx, h, callback, cookie);
}

static inline void async_handle_vmar(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_vmar_callback callback, void* cookie) {
    proto->ops->vmar(proto->ctx, h, callback, cookie);
}

static inline void async_handle_fifo(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_fifo_callback callback, void* cookie) {
    proto->ops->fifo(proto->ctx, h, callback, cookie);
}

static inline void async_handle_guest(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_guest_callback callback, void* cookie) {
    proto->ops->guest(proto->ctx, h, callback, cookie);
}

static inline void async_handle_timer(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_timer_callback callback, void* cookie) {
    proto->ops->timer(proto->ctx, h, callback, cookie);
}

static inline void async_handle_profile(const async_handle_protocol_t* proto, zx_handle_t h, async_handle_profile_callback callback, void* cookie) {
    proto->ops->profile(proto->ctx, h, callback, cookie);
}


typedef struct synchronous_handle_protocol_ops {
    void (*handle)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*process)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*thread)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*vmo)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*channel)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*event)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*port)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*interrupt)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*debug_log)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*socket)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*resource)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*event_pair)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*job)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*vmar)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*fifo)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*guest)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*timer)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
    void (*profile)(void* ctx, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2);
} synchronous_handle_protocol_ops_t;


struct synchronous_handle_protocol {
    synchronous_handle_protocol_ops_t* ops;
    void* ctx;
};

static inline void synchronous_handle_handle(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->handle(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_process(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->process(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_thread(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->thread(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_vmo(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->vmo(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_channel(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->channel(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_event(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->event(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_port(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->port(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_interrupt(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->interrupt(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_debug_log(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->debug_log(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_socket(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->socket(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_resource(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->resource(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_event_pair(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->event_pair(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_job(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->job(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_vmar(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->vmar(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_fifo(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->fifo(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_guest(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->guest(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_timer(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->timer(proto->ctx, h, out_h, out_h2);
}

static inline void synchronous_handle_profile(const synchronous_handle_protocol_t* proto, zx_handle_t h, zx_handle_t* out_h, zx_handle_t* out_h2) {
    proto->ops->profile(proto->ctx, h, out_h, out_h2);
}



__END_CDECLS
