// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <kernel/event.h>
#include <kernel/mutex.h>
#include <list.h>
#include <stdint.h>

__BEGIN_CDECLS

// clang-format off
#define DLOG_LEVEL_SPEW     0x0000
#define DLOG_LEVEL_INFO     0x0002
#define DLOG_LEVEL_WARN     0x0007
#define DLOG_LEVEL_ERROR    0x000E
#define DLOG_LEVEL_FATAL    0x000F
#define DLOG_LEVEL(n)       ((n) & 15)

#define DLOG_FLAG_KERNEL    0x0100
#define DLOG_FLAG_DEVMGR    0x0200
#define DLOG_FLAG_CONSOLE   0x0400
#define DLOG_FLAG_DEVICE    0x0800
#define DLOG_FLAG_MASK      0x0F00

#define DLOG_FLAG_WAIT      0x80000000

#define DLOG_MAX_ENTRY      256
// clang-format on

typedef struct dlog dlog_t;
typedef struct dlog_record dlog_record_t;
typedef struct dlog_reader dlog_reader_t;

struct dlog {
    mutex_t lock;

    uint32_t size;
    uint32_t head;
    uint32_t tail;
    bool paused;
    void* data;

    struct list_node readers;
};

struct dlog_reader {
    struct list_node node;
    event_t event;
    dlog_t* log;
    uint32_t tail;
};

struct dlog_record {
    uint32_t next;
    uint16_t datalen;
    uint16_t flags;
    uint64_t timestamp;
    char data[0];
};

void dlog_reader_init(dlog_reader_t* rdr);
void dlog_reader_destroy(dlog_reader_t* rdr);
status_t dlog_write(uint32_t flags, const void* ptr, size_t len);
status_t dlog_read_etc(dlog_reader_t* rdr, uint32_t flags, void* ptr, size_t len, bool user);
static inline status_t dlog_read(dlog_reader_t* rdr, uint32_t flags, void* ptr, size_t len) {
    return dlog_read_etc(rdr, flags, ptr, len, false);
}
static inline status_t dlog_read_user(dlog_reader_t* rdr, uint32_t flags, void* uptr, size_t len) {
    return dlog_read_etc(rdr, flags, uptr, len, true);
}
void dlog_wait(dlog_reader_t* rdr);

void dlog_bluescreen(void);

__END_CDECLS
