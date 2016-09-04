// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <assert.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

// clang-format off

#define KTRACE_TAG(event, group) ((((group)&0xFFF)<<20)|(((event)&0xFFF)<<8))

#define KTRACE_CPUID(tag) ((tag)&0x3F)
#define KTRACE_GROUP(tag) (((tag)>>20)&0xFFF)
#define KTRACE_EVENT(tag) (((tag)>>8)&0xFFF)

#define KTRACE_RECSIZE    (32)
#define KTRACE_NAMESIZE   (24)
#define KTRACE_NAMEOFF    (8)

#define KTRACE_VERSION    (0x00010000)

typedef struct ktrace_record {
    uint32_t tag;
    uint32_t id;
    uint64_t ts;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
} ktrace_record_t;

static_assert(sizeof(ktrace_record_t) == KTRACE_RECSIZE,
              "ktrace_record_t is not KTRACE_RECSIZE bytes");

// Filter Groups

#define GRP_ALL               0xFFF
#define GRP_META              0x001
#define GRP_LIFECYCLE         0x002
#define GRP_SCHEDULER         0x004
#define GRP_TASKS             0x008
#define GRP_IPC               0x010

#define GRP_MASK(grp)         ((grp) << 20)

// Events, Combined with their Filter Group

#define TAG_VERSION           KTRACE_TAG(0x000, GRP_META) // version
#define TAG_TICKS_PER_MS      KTRACE_TAG(0x001, GRP_META) // lo32 hi32

#define TAG_CONTEXT_SWITCH    KTRACE_TAG(0x010, GRP_SCHEDULER) // to-tid (tstate<<16)|cpuid from-kt to-kt

#define TAG_OBJECT_DELETE     KTRACE_TAG(0x011, GRP_LIFECYCLE) // id

#define TAG_THREAD_CREATE     KTRACE_TAG(0x030, GRP_TASKS) // tid pid
#define TAG_THREAD_NAME       KTRACE_TAG(0x031, GRP_TASKS) // tid name[24]
#define TAG_THREAD_START      KTRACE_TAG(0x032, GRP_TASKS) // tid
#define TAG_THREAD_EXIT       KTRACE_TAG(0x033, GRP_TASKS)

#define TAG_PROC_CREATE       KTRACE_TAG(0x040, GRP_TASKS) // pid
#define TAG_PROC_NAME         KTRACE_TAG(0x041, GRP_TASKS) // pid name[24]
#define TAG_PROC_START        KTRACE_TAG(0x042, GRP_TASKS) // tid pid
#define TAG_PROC_EXIT         KTRACE_TAG(0x043, GRP_TASKS) // pid

#define TAG_MSGPIPE_CREATE    KTRACE_TAG(0x050, GRP_IPC) // id0 id1 flags
#define TAG_MSGPIPE_WRITE     KTRACE_TAG(0x051, GRP_IPC) // id0 bytes handles
#define TAG_MSGPIPE_READ      KTRACE_TAG(0x052, GRP_IPC) // id1 bytes handles

#define TAG_PORT_CREATE       KTRACE_TAG(0x060, GRP_IPC) // id
#define TAG_PORT_QUEUE        KTRACE_TAG(0x061, GRP_IPC) // id size
#define TAG_PORT_WAIT         KTRACE_TAG(0x062, GRP_IPC) // id
#define TAG_PORT_WAIT_DONE    KTRACE_TAG(0x063, GRP_IPC) // id status

#define TAG_WAIT_ONE          KTRACE_TAG(0x070, GRP_IPC) // id signals timeoutlo timeouthi
#define TAG_WAIT_ONE_DONE     KTRACE_TAG(0x071, GRP_IPC) // id status pending

// Actions for ktrace control

#define KTRACE_ACTION_START    1 // options = grpmask, 0 = all
#define KTRACE_ACTION_STOP     2 // options ignored
#define KTRACE_ACTION_REWIND   3 // options ignored

__END_CDECLS