// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>

#include <err.h>
#include <dev/udisplay.h>
#include <kernel/thread.h>
#include <lib/user_copy.h>
#include <lib/io.h>
#include <lk/init.h>
#include <platform.h>
#include <string.h>

#define DLOG_SIZE (64 * 1024)

static uint8_t DLOG_DATA[DLOG_SIZE];

static dlog_t DLOG = {
    .lock = MUTEX_INITIAL_VALUE(DLOG.lock),
    .size = DLOG_SIZE,
    .data = DLOG_DATA,
    .readers = LIST_INITIAL_VALUE(DLOG.readers),
};

#define MAX_DATA_SIZE (DLOG_MAX_ENTRY - sizeof(dlog_record_t))

#define ALIGN8(n) (((n) + 7) & (~7))

#define REC(ptr, off) ((dlog_record_t*)((ptr) + (off)))

// To avoid complexity with splitting record headers, this is
// a mostly-circular buffer -- each record has a next index
// that can be used to advance to the next record, allowing the
// leftover space not large enough for a full record at the end
// to be skipped easily.
status_t dlog_write(uint32_t flags, const void* ptr, size_t len) {
    dlog_t* log = &DLOG;

    if (arch_ints_disabled() || log->paused) {
        return ERR_BAD_STATE;
    }

    if (len > MAX_DATA_SIZE) {
        return ERR_OUT_OF_RANGE;
    }

    // Keep record headers uint64 aligned
    size_t sz = ALIGN8(len + sizeof(dlog_record_t));

    mutex_acquire(&log->lock);

    // Determine location of new record and next record after it
    uint32_t dst = log->head;
    uint32_t end = log->head + sz;
    uint32_t nxt = end;
    if ((nxt + DLOG_MAX_ENTRY) > log->size) {
        nxt = 0;
    }

    // Advance our tail if we write past it.
    if (log->head != log->tail) {
        while ((log->tail >= dst) && (log->tail < end)) {
            log->tail = REC(log->data, log->tail)->next;
        }
    }

    // Notify readers and advance their tails if necessary
    dlog_reader_t* rdr;
    list_for_every_entry (&log->readers, rdr, dlog_reader_t, node) {
        if (rdr->tail == log->head) {
            // Reader view was empty, point reader's tail
            // at the message we're writing now
            rdr->tail = dst;
        } else {
            // If we're about to overwrite the message at
            // the reader's tail, advance that tail.
            // This will never cause the reader view to be "empty".
            while ((rdr->tail >= dst) && (rdr->tail < end)) {
                rdr->tail = REC(log->data, rdr->tail)->next;
            }
        }
        event_signal(&rdr->event, false);
    }

    // Write the new record
    dlog_record_t* rec = REC(log->data, dst);
    rec->next = nxt;
    rec->datalen = len;
    rec->flags = flags;
    rec->timestamp = current_time_hires();
    memcpy(rec->data, ptr, len);

    // Advance the head pointer
    log->head = nxt;

    mutex_release(&log->lock);
    return NO_ERROR;
}

// TODO: support reading multiple messages at a time
// TODO: filter with flags
status_t dlog_read_etc(dlog_reader_t* rdr, uint32_t flags, void* ptr, size_t len, bool user) {
    dlog_t* log = rdr->log;
    status_t r = ERR_BAD_STATE;

    mutex_acquire(&log->lock);
    if (rdr->tail != log->head) {
        dlog_record_t* rec = REC(log->data, rdr->tail);
        size_t copylen = rec->datalen + sizeof(dlog_record_t);
        if (copylen > len) {
            r = ERR_BUFFER_TOO_SMALL;
        } else {
            if (user) {
                r = copy_to_user_unsafe(ptr, rec, copylen);
                if (r == NO_ERROR) {
                    r = copylen;
                }
            } else {
                memcpy(ptr, rec, copylen);
                r = copylen;
            }
            rdr->tail = rec->next;
            if (rdr->tail == log->head) {
                // Nothing left to read, we're in the "empty" state now.
                event_unsignal(&rdr->event);
            }
        }
    }
    mutex_release(&log->lock);
    return r;
}

void dlog_reader_init(dlog_reader_t* rdr) {
    dlog_t* log = &DLOG;

    rdr->log = log;
    event_init(&rdr->event, false, 0);

    mutex_acquire(&log->lock);
    list_add_tail(&log->readers, &rdr->node);
    rdr->tail = log->tail;
    if (log->head != log->tail) {
        event_signal(&rdr->event, false);
    }
    mutex_release(&log->lock);
}

void dlog_reader_destroy(dlog_reader_t* rdr) {
    dlog_t* log = rdr->log;

    mutex_acquire(&log->lock);
    list_delete(&rdr->node);
    event_destroy(&rdr->event);
    mutex_release(&log->lock);
}

void dlog_wait(dlog_reader_t* rdr) {
    event_wait(&rdr->event);
}

static void cputs(const char* data, size_t len) {
    while (len-- > 0) {
        char c = *data++;
        if (c != '\n') {
            platform_dputc(c);
        }
    }
}

static int debuglog_reader(void* arg) {
    uint8_t buffer[DLOG_MAX_ENTRY + 1];
    char tmp[DLOG_MAX_ENTRY + 64];
    dlog_record_t* rec = (dlog_record_t*)buffer;
    dlog_reader_t reader;
    int n;

    dlog_reader_init(&reader);
    for (;;) {
        dlog_wait(&reader);
        while (dlog_read(&reader, 0, rec, DLOG_MAX_ENTRY) > 0) {
            if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
                rec->data[rec->datalen - 1] = 0;
            } else {
                rec->data[rec->datalen] = 0;
            }
            n = snprintf(tmp, sizeof(tmp), "[%05d.%03d] %c %s\n",
                         (int) (rec->timestamp / 1000000000ULL),
                         (int) ((rec->timestamp / 1000000ULL) % 1000ULL),
                         (rec->flags & DLOG_FLAG_KERNEL) ? 'K' : 'U',
                         rec->data);
            if (n > (int)sizeof(tmp)) {
                n = sizeof(tmp);
            }
            __kernel_console_write(tmp, n);
            __kernel_serial_write(tmp, n);
        }
    }
    return NO_ERROR;
}

void dlog_bluescreen(void) {
    udisplay_bind_gfxconsole();

    DLOG.paused = true;

    uint8_t buffer[DLOG_MAX_ENTRY + 1];
    dlog_record_t* rec = (dlog_record_t*)buffer;
    dlog_reader_t reader;

    dlog_reader_init(&reader);
    while (dlog_read(&reader, 0, rec, DLOG_MAX_ENTRY) > 0) {
        rec->data[rec->datalen] = 0;
        if (rec->datalen && (rec->data[rec->datalen - 1] == '\n')) {
            rec->data[rec->datalen - 1] = 0;
        }
        dprintf(INFO, "[%05d.%03d] %c %s\n",
                (int) (rec->timestamp / 1000000000ULL),
                (int) ((rec->timestamp / 1000000ULL) % 1000ULL),
                (rec->flags & DLOG_FLAG_KERNEL) ? 'K' : 'U',
                rec->data);
    }
}

static void dlog_init_hook(uint level) {
    thread_t* rthread = thread_create("debuglog-reader", debuglog_reader, NULL,
                                      HIGH_PRIORITY - 1, DEFAULT_STACK_SIZE);
    if (rthread) {
        thread_resume(rthread);
    }
}

LK_INIT_HOOK(debuglog, dlog_init_hook, LK_INIT_LEVEL_THREADING - 1);
