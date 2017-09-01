// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>

#include <err.h>
#include <dev/udisplay.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <lib/io.h>
#include <lib/version.h>
#include <lk/init.h>
#include <platform.h>
#include <string.h>

#define DLOG_SIZE (128u * 1024u)
#define DLOG_MASK (DLOG_SIZE - 1u)

static_assert((DLOG_SIZE & DLOG_MASK) == 0u, "must be power of two");
static_assert(DLOG_MAX_RECORD <= DLOG_SIZE, "wat");
static_assert((DLOG_MAX_RECORD & 3) == 0, "E_DONT_DO_THAT");

static uint8_t DLOG_DATA[DLOG_SIZE];

static dlog_t DLOG = {
    .lock = SPIN_LOCK_INITIAL_VALUE,
    .head = 0,
    .tail = 0,
    .data = DLOG_DATA,
    .event = EVENT_INITIAL_VALUE(DLOG.event, 0, EVENT_FLAG_AUTOUNSIGNAL),

    .readers_lock = MUTEX_INITIAL_VALUE(DLOG.readers_lock),
    .readers = LIST_INITIAL_VALUE(DLOG.readers),
};

// The debug log maintains a circular buffer of debug log records,
// consisting of a common header (dlog_header_t) followed by up
// to 224 bytes of textual log message.  Records are aligned on
// uint32_t boundaries, so the header word which indicates the
// true size of the record and the space it takes in the fifo
// can always be read with a single uint32_t* read (the header
// or body may wrap but the initial header word never does).
//
// The ring buffer position is maintained by continuously incrementing
// head and tail pointers (type size_t, so uint64_t on 64bit systems),
//
// This allows readers to trivial compute if their local tail
// pointer has "fallen out" of the fifo (an entire fifo's worth
// of messages were written since they last tried to read) and then
// they can snap their tail to the global tail and restart
//
//
// Tail indicates the oldest message in the debug log to read
// from, Head indicates the next space in the debug log to write
// a new message to.  They are clipped to the actual buffer by
// DLOG_MASK.
//
//       T                     T
//  [....XXXX....]  [XX........XX]
//           H         H


#define ALIGN4(n) (((n) + 3) & (~3))

mx_status_t dlog_write(uint32_t flags, const void* ptr, size_t len) {
    dlog_t* log = &DLOG;

    if (len > DLOG_MAX_DATA) {
        return MX_ERR_OUT_OF_RANGE;
    }

    if (log->panic) {
        return MX_ERR_BAD_STATE;
    }

    // Our size "on the wire" must be a multiple of 4, so we know
    // that worst case there will be room for a header skipping
    // the last n bytes when the fifo wraps
    size_t wiresize = DLOG_MIN_RECORD + ALIGN4(len);

    // Prepare the record header before taking the lock
    dlog_header_t hdr;
    hdr.header = DLOG_HDR_SET(wiresize, DLOG_MIN_RECORD + len);
    hdr.datalen = len;
    hdr.flags = flags;
    hdr.timestamp = current_time();
    thread_t *t = get_current_thread();
    if (t) {
        hdr.pid = t->user_pid;
        hdr.tid = t->user_tid;
    } else {
        hdr.pid = 0;
        hdr.tid = 0;
    }

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&log->lock, state);

    // Discard records at tail until there is enough
    // space for the new record.
    while ((log->head - log->tail) > (DLOG_SIZE - wiresize)) {
        uint32_t header = *((uint32_t*) (log->data + (log->tail & DLOG_MASK)));
        log->tail += DLOG_HDR_GET_FIFOLEN(header);
    }

    size_t offset = (log->head & DLOG_MASK);

    size_t fifospace = DLOG_SIZE - offset;

    if (fifospace >= wiresize) {
        // everything fits in one write, simple case!
        memcpy(log->data + offset, &hdr, sizeof(hdr));
        memcpy(log->data + offset + sizeof(hdr), ptr, len);
    } else if (fifospace < sizeof(hdr)) {
        // the wrap happens in the header
        memcpy(log->data + offset, &hdr, fifospace);
        memcpy(log->data, ((void*) &hdr) + fifospace, sizeof(hdr) - fifospace);
        memcpy(log->data + (sizeof(hdr) - fifospace), ptr, len);
    } else {
        // the wrap happens in the data
        memcpy(log->data + offset, &hdr, sizeof(hdr));
        offset += sizeof(hdr);
        fifospace -= sizeof(hdr);
        memcpy(log->data + offset, ptr, fifospace);
        memcpy(log->data, ptr + fifospace, len - fifospace);
    }
    log->head += wiresize;

    // if we happen to be called from within the global thread lock, use a
    // special version of event signal
    if (spin_lock_holder_cpu(&thread_lock) == arch_curr_cpu_num()) {
        event_signal_thread_locked(&log->event);
    } else {
        event_signal(&log->event, false);
    }

    spin_unlock_irqrestore(&log->lock, state);

    return MX_OK;
}

// TODO: support reading multiple messages at a time
// TODO: filter with flags
mx_status_t dlog_read(dlog_reader_t* rdr, uint32_t flags, void* ptr, size_t len, size_t* _actual) {
    // must be room for worst-case read
    if (len < DLOG_MAX_RECORD) {
        return MX_ERR_BUFFER_TOO_SMALL;
    }

    dlog_t* log = rdr->log;
    mx_status_t status = MX_ERR_SHOULD_WAIT;

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&log->lock, state);

    size_t rtail = rdr->tail;

    // If the read-tail is not within the range of log-tail..log-head
    // this reader has been lapped by a writer and we reset our read-tail
    // to the current log-tail.
    //
    if ((log->head - log->tail) < (log->head - rtail)) {
        rtail = log->tail;
    }

    if (rtail != log->head) {
        size_t offset = (rtail & DLOG_MASK);
        uint32_t header = *((uint32_t*) (log->data + offset));

        size_t actual = DLOG_HDR_GET_READLEN(header);
        size_t fifospace = DLOG_SIZE - offset;

        if (fifospace >= actual) {
            memcpy(ptr, log->data + offset, actual);
        } else {
            memcpy(ptr, log->data + offset, fifospace);
            memcpy(ptr + fifospace, log->data, actual - fifospace);
        }

        *_actual = actual;
        status = MX_OK;

        rtail += DLOG_HDR_GET_FIFOLEN(header);
    }

    rdr->tail = rtail;

    spin_unlock_irqrestore(&log->lock, state);

    return status;
}

void dlog_reader_init(dlog_reader_t* rdr, void (*notify)(void*), void* cookie) {
    dlog_t* log = &DLOG;

    rdr->log = log;
    rdr->notify = notify;
    rdr->cookie = cookie;

    mutex_acquire(&log->readers_lock);
    list_add_tail(&log->readers, &rdr->node);

    bool do_notify = false;

    spin_lock_saved_state_t state;
    spin_lock_irqsave(&log->lock, state);
    rdr->tail = log->tail;
    do_notify = (log->tail != log->head);
    spin_unlock_irqrestore(&log->lock, state);

    // simulate notify callback for events that arrived
    // before we were initialized
    if(do_notify && notify) {
        notify(cookie);
    }

    mutex_release(&log->readers_lock);
}

void dlog_reader_destroy(dlog_reader_t* rdr) {
    dlog_t* log = rdr->log;

    mutex_acquire(&log->readers_lock);
    list_delete(&rdr->node);
    mutex_release(&log->readers_lock);
}


// The debuglog notifier thread observes when the debuglog is
// written and calls the notify callback on any readers that
// have one so they can process new log messages.
static int debuglog_notifier(void* arg) {
    dlog_t* log = &DLOG;

    for (;;) {
        event_wait(&log->event);

        // notify readers that new log items were posted
        mutex_acquire(&log->readers_lock);
        dlog_reader_t* rdr;
        list_for_every_entry (&log->readers, rdr, dlog_reader_t, node) {
            if (rdr->notify) {
                rdr->notify(rdr->cookie);
            }
        }
        mutex_release(&log->readers_lock);
    }
    return MX_OK;
}


// The debuglog dumper thread creates a reader to observe
// debuglog writes and dump them to the kernel consoles
// and kernel serial console.
static void debuglog_dumper_notify(void* cookie) {
    event_t* event = cookie;
    event_signal(event, false);
}

static int debuglog_dumper(void *arg) {
    // assembly buffer with room for log text plus header text
    char tmp[DLOG_MAX_DATA + 128];

    struct {
        dlog_header_t hdr;
        char data[DLOG_MAX_DATA + 1];
    } rec;

    event_t event = EVENT_INITIAL_VALUE(event, 0, EVENT_FLAG_AUTOUNSIGNAL);

    dlog_reader_t reader;
    dlog_reader_init(&reader, debuglog_dumper_notify, &event);

    for (;;) {
        event_wait(&event);

        // dump records to kernel console
        size_t actual;
        while (dlog_read(&reader, 0, &rec, DLOG_MAX_RECORD, &actual) == MX_OK) {
            if (rec.hdr.datalen && (rec.data[rec.hdr.datalen - 1] == '\n')) {
                rec.data[rec.hdr.datalen - 1] = 0;
            } else {
                rec.data[rec.hdr.datalen] = 0;
            }
            int n;
            n = snprintf(tmp, sizeof(tmp), "[%05d.%03d] %05" PRIu64 ".%05" PRIu64 "> %s\n",
                         (int) (rec.hdr.timestamp / LK_SEC(1)),
                         (int) ((rec.hdr.timestamp / LK_MSEC(1)) % 1000ULL),
                         rec.hdr.pid, rec.hdr.tid, rec.data);
            if (n > (int)sizeof(tmp)) {
                n = sizeof(tmp);
            }
            __kernel_console_write(tmp, n);
            __kernel_serial_write(tmp, n);
        }
    }

    return 0;
}


void dlog_bluescreen_init(void) {
    // if we're panicing, stop processing log writes
    // they'll fail over to kernel console and serial
    DLOG.panic = true;

    udisplay_bind_gfxconsole();

    // replay debug log?

    dprintf(INFO, "\nMAGENTA KERNEL PANIC\n\nUPTIME: %" PRIu64 "ms\n",
            current_time() / LK_MSEC(1));
    dprintf(INFO, "BUILDID %s\n\n", version.buildid);

    // Log the ELF build ID in the format the symbolizer scripts understand.
    if (version.elf_build_id[0] != '\0') {
        vaddr_t base = KERNEL_BASE + KERNEL_LOAD_OFFSET;
        dprintf(INFO, "dso: id=%s base=%#lx name=magenta.elf\n",
                version.elf_build_id, base);
    }
}

static void dlog_init_hook(uint level) {
    thread_t* rthread;

    if ((rthread = thread_create("debuglog-notifier", debuglog_notifier, NULL,
                                 HIGH_PRIORITY - 1, DEFAULT_STACK_SIZE)) != NULL) {
        thread_resume(rthread);
    }
    if ((rthread = thread_create("debuglog-dumper", debuglog_dumper, NULL,
                                 HIGH_PRIORITY - 2, DEFAULT_STACK_SIZE)) != NULL) {
        thread_resume(rthread);
    }
}

LK_INIT_HOOK(debuglog, dlog_init_hook, LK_INIT_LEVEL_THREADING - 1);
