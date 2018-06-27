// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include <inet6/inet6.h>
#include <launchpad/launchpad.h>
#include <sync/completion.h>
#include <tftp/tftp.h>
#include <zircon/assert.h>
#include <zircon/boot/netboot.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "netsvc.h"

#define SCRATCHSZ 2048

#define TFTP_TIMEOUT_SECS 1

#define NB_IMAGE_PREFIX_LEN (strlen(NB_IMAGE_PREFIX))
#define NB_FILENAME_PREFIX_LEN (strlen(NB_FILENAME_PREFIX))

// Identifies what the file being streamed over TFTP should be
// used for.
typedef enum netfile_type {
    netboot, // A bootfs file
    paver,   // A disk image which should be paved to disk
} netfile_type_t;

typedef struct {
    bool is_write;
    char filename[PATH_MAX + 1];
    netfile_type_t type;
    union {
        nbfile* netboot_file;
        struct {
            int fd;                     // Pipe to paver process
            size_t size;                // Total size of file
            zx_handle_t process;

            // Buffer used for stashing data from tftp until it can be written out to the paver
            zx_handle_t buffer_handle;
            uint8_t* buffer;
            atomic_uint buf_refcount;
            atomic_size_t offset;       // Buffer write offset (read offset is stored locally)
            thrd_t buf_copy_thrd;
            completion_t data_ready;    // Allows read thread to block on buffer writes
        } paver;
    };
} file_info_t;

typedef struct {
    ip6_addr_t dest_addr;
    uint16_t dest_port;
    uint32_t timeout_ms;
} transport_info_t;

static char tftp_session_scratch[SCRATCHSZ];
char tftp_out_scratch[SCRATCHSZ];

static size_t last_msg_size = 0;
static tftp_session* session = NULL;
static file_info_t file_info;
static transport_info_t transport_info;

atomic_bool paving_in_progress = false;
zx_time_t tftp_next_timeout = ZX_TIME_INFINITE;

static ssize_t file_open_read(const char* filename, void* cookie) {
    // Make sure all in-progress paving options have completed
    if (atomic_load(&paving_in_progress) == true) {
        return TFTP_ERR_SHOULD_WAIT;
    }
    file_info_t* file_info = cookie;
    file_info->is_write = false;
    strncpy(file_info->filename, filename, PATH_MAX);
    file_info->filename[PATH_MAX] = '\0';
    file_info->netboot_file = NULL;
    size_t file_size;
    if (netfile_open(filename, O_RDONLY, &file_size) == 0) {
        return (ssize_t)file_size;
    }
    return TFTP_ERR_NOT_FOUND;
}

static zx_status_t alloc_paver_buffer(file_info_t* file_info, size_t size) {
    zx_status_t status;
    status = zx_vmo_create(size, 0, &file_info->paver.buffer_handle);
    if (status != ZX_OK) {
        printf("netsvc: unable to allocate buffer VMO\n");
        return status;
    }
    zx_object_set_property(file_info->paver.buffer_handle, ZX_PROP_NAME, "paver", 5);
    uintptr_t buffer;
    status = zx_vmar_map(zx_vmar_root_self(), 0, file_info->paver.buffer_handle, 0, size,
                         ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE, &buffer);
    if (status != ZX_OK) {
        printf("netsvc: unable to map buffer\n");
        zx_handle_close(file_info->paver.buffer_handle);
        return status;
    }
    file_info->paver.buffer = (uint8_t*)buffer;
    return ZX_OK;
}

static zx_status_t dealloc_paver_buffer(file_info_t* file_info) {
    zx_status_t status = zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)file_info->paver.buffer,
                                       file_info->paver.size);
    if (status != ZX_OK) {
        printf("netsvc: failed to unmap paver buffer: %s\n", zx_status_get_string(status));
        goto done;
    }

    status = zx_handle_close(file_info->paver.buffer_handle);
    if (status != ZX_OK) {
        printf("netsvc: failed to close paver buffer handle: %s\n", zx_status_get_string(status));
    }

done:
    file_info->paver.buffer = NULL;
    return status;
}

static int drain_pipe(void* arg) {
    char buf[4096];
    int fd = (uintptr_t)arg;

    ssize_t sz;
    while ((sz = read(fd, buf, sizeof(buf) - 1)) > 0) {
        // ensure null termination
        buf[sz] = '\0';
        printf("%s", buf);
    }

    close(fd);
    return sz;
}

// Pushes all data from the paver buffer (filled by netsvc) into the paver input pipe. When
// there's no data to copy, blocks on data_ready until more data is written into the buffer.
static int paver_copy_buffer(void* arg) {
    file_info_t* file_info = arg;
    size_t read_ndx = 0;
    int result = 0;
    zx_time_t last_reported = zx_clock_get_monotonic();
    while (read_ndx < file_info->paver.size) {
        completion_reset(&file_info->paver.data_ready);
        size_t write_ndx = atomic_load(&file_info->paver.offset);
        if (write_ndx == read_ndx) {
            // Wait for more data to be written -- we are allowed up to 3 tftp timeouts before
            // a connection is dropped, so we should wait at least that long before giving up.
            if (completion_wait(&file_info->paver.data_ready, ZX_SEC(5 * TFTP_TIMEOUT_SECS))
                == ZX_OK) {
                continue;
            }
            printf("netsvc: timed out while waiting for data in paver-copy thread\n");
            result = TFTP_ERR_TIMED_OUT;
            goto done;
        }
        while(read_ndx < write_ndx) {
            int r = write(file_info->paver.fd, &file_info->paver.buffer[read_ndx],
                          write_ndx - read_ndx);
            if (r <= 0) {
                printf("netsvc: couldn't write to paver fd: %d\n", r);
                result = TFTP_ERR_IO;
                goto done;
            }
            read_ndx += r;
            zx_time_t curr_time = zx_clock_get_monotonic();
            if ((curr_time - last_reported) >= ZX_SEC(1)) {
                float complete = ((float)read_ndx / (float)file_info->paver.size) * 100.0;
                printf("netsvc: paver write progress %0.1f%%\n", complete);
                last_reported = curr_time;
            }
        }
    }
done:
    close(file_info->paver.fd);

    unsigned int refcount = atomic_fetch_sub(&file_info->paver.buf_refcount, 1);
    if (refcount == 1) {
        dealloc_paver_buffer(file_info);
    }
    // If all of the data has been written out to the paver process wait for it to complete
    if (result == 0) {
        zx_signals_t signals;
        zx_object_wait_one(file_info->paver.process, ZX_TASK_TERMINATED,
                           zx_deadline_after(ZX_SEC(10)), &signals);
    }
    zx_handle_close(file_info->paver.process);

    // Extra protection against double-close.
    file_info->filename[0] = '\0';
    atomic_store(&paving_in_progress, false);
    return result;
}

static tftp_status paver_open_write(const char* filename, size_t size, file_info_t* file_info) {
    // paving an image to disk
    zx_status_t status;
    launchpad_t* lp;
    launchpad_create(0, "paver", &lp);
    const char* bin = "/boot/bin/install-disk-image";
    launchpad_load_from_file(lp, bin);
    if (!strcmp(filename + NB_IMAGE_PREFIX_LEN, NB_FVM_HOST_FILENAME)) {
        printf("netsvc: Running FVM Paver\n");
        const char* args[] = {bin, "install-fvm"};
        launchpad_set_args(lp, 2, args);
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN, NB_EFI_HOST_FILENAME)) {
        printf("netsvc: Running EFI Paver\n");
        const char* args[] = {bin, "install-efi"};
        launchpad_set_args(lp, 2, args);
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN, NB_KERNC_HOST_FILENAME)) {
        printf("netsvc: Running KERN-C Paver\n");
        const char* args[] = {bin, "install-kernc"};
        launchpad_set_args(lp, 2, args);
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN, NB_ZIRCONA_HOST_FILENAME)) {
        printf("netsvc: Running ZIRCON-A Paver\n");
        const char* args[] = {bin, "install-zircona"};
        launchpad_set_args(lp, 2, args);
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN, NB_ZIRCONB_HOST_FILENAME)) {
        printf("netsvc: Running ZIRCON-B Paver\n");
        const char* args[] = {bin, "install-zirconb"};
        launchpad_set_args(lp, 2, args);
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN, NB_ZIRCONR_HOST_FILENAME)) {
        printf("netsvc: Running ZIRCON-R Paver\n");
        const char* args[] = {bin, "install-zirconr"};
        launchpad_set_args(lp, 2, args);
    } else {
        fprintf(stderr, "netsvc: Unknown Paver\n");
        return TFTP_ERR_IO;
    }
    launchpad_clone(lp, LP_CLONE_FDIO_NAMESPACE | LP_CLONE_FDIO_STDIO | LP_CLONE_ENVIRON);

    int fds[2];
    if (pipe(fds)) {
        return TFTP_ERR_IO;
    }
    launchpad_transfer_fd(lp, fds[0], STDIN_FILENO);

    int logfds[2];
    if (pipe(logfds)) {
        return TFTP_ERR_IO;
    }
    launchpad_transfer_fd(lp, logfds[1], STDERR_FILENO);

    if ((status = launchpad_go(lp, &file_info->paver.process, NULL)) != ZX_OK) {
        printf("netsvc: tftp couldn't launch paver\n");
        goto err_close_fds;
    }

    thrd_t log_thrd;
    if ((thrd_create(&log_thrd, drain_pipe, (void*)(uintptr_t)logfds[0])) == thrd_success) {
        thrd_detach(log_thrd);
    } else {
        printf("netsvc: couldn't create paver log message redirection thread\n");
        goto err_close_fds;
    }

    if ((status = alloc_paver_buffer(file_info, size)) != ZX_OK) {
        goto err_close_fds;
    }

    file_info->type = paver;
    file_info->paver.fd = fds[1];
    file_info->paver.size = size;
    // Both the netsvc thread and the paver copy thread access the buffer, and either
    // may be done with it first so we use a refcount to decide when to deallocate it
    atomic_store(&file_info->paver.buf_refcount, 2);
    atomic_store(&file_info->paver.offset, 0);
    atomic_store(&paving_in_progress, true);

    if ((thrd_create(&file_info->paver.buf_copy_thrd, paver_copy_buffer, (void*)file_info))
        != thrd_success) {
        printf("netsvc: unable to launch buffer copy thread\n");
        status = ZX_ERR_NO_RESOURCES;
        goto dealloc_buffer;
    }
    thrd_detach(file_info->paver.buf_copy_thrd);

    return TFTP_NO_ERROR;

dealloc_buffer:
    dealloc_paver_buffer(file_info);

err_close_fds:
    close(fds[1]);
    close(logfds[0]);
    return status;
}

static tftp_status file_open_write(const char* filename, size_t size,
                                   void* cookie) {
    // Make sure all in-progress paving options have completed
    if (atomic_load(&paving_in_progress) == true) {
        return TFTP_ERR_SHOULD_WAIT;
    }
    file_info_t* file_info = cookie;
    file_info->is_write = true;
    strncpy(file_info->filename, filename, PATH_MAX);
    file_info->filename[PATH_MAX] = '\0';

    if (netbootloader && !strncmp(filename, NB_FILENAME_PREFIX, NB_FILENAME_PREFIX_LEN)) {
        // netboot
        file_info->type = netboot;
        file_info->netboot_file = netboot_get_buffer(filename, size);
        if (file_info->netboot_file != NULL) {
            return TFTP_NO_ERROR;
        }
    } else if (netbootloader & !strncmp(filename, NB_IMAGE_PREFIX, NB_IMAGE_PREFIX_LEN)) {
        // paver
        tftp_status status = paver_open_write(filename, size, file_info);
        if (status != TFTP_NO_ERROR) {
            file_info->filename[0] = '\0';
        }
        return status;
    } else {
        // netcp
        if (netfile_open(filename, O_WRONLY, NULL) == 0) {
            return TFTP_NO_ERROR;
        }
    }
    return TFTP_ERR_INVALID_ARGS;
}

static tftp_status file_read(void* data, size_t* length, off_t offset, void* cookie) {
    if (length == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }
    int read_len = netfile_offset_read(data, offset, *length);
    if (read_len < 0) {
        return TFTP_ERR_IO;
    }
    *length = read_len;
    return TFTP_NO_ERROR;
}

static tftp_status file_write(const void* data, size_t* length, off_t offset, void* cookie) {
    if (length == NULL) {
        return TFTP_ERR_INVALID_ARGS;
    }
    file_info_t* file_info = cookie;
    if (file_info->type == netboot && file_info->netboot_file != NULL) {
        nbfile* nb_file = file_info->netboot_file;
        if (((size_t)offset > nb_file->size) || (offset + *length) > nb_file->size) {
            return TFTP_ERR_INVALID_ARGS;
        }
        memcpy(nb_file->data + offset, data, *length);
        nb_file->offset = offset + *length;
        return TFTP_NO_ERROR;
    } else if (file_info->type == paver) {
        if (!atomic_load(&paving_in_progress)) {
          printf("netsvc: paver exited prematurely\n");
          return TFTP_ERR_IO;
        }

        if (((size_t)offset > file_info->paver.size)
            || (offset + *length) > file_info->paver.size) {
            return TFTP_ERR_INVALID_ARGS;
        }
        memcpy(&file_info->paver.buffer[offset], data, *length);
        size_t new_offset = offset + *length;
        atomic_store(&file_info->paver.offset, new_offset);
        // Wake the paver thread, if it is waiting for data
        completion_signal(&file_info->paver.data_ready);
        return TFTP_NO_ERROR;
    } else {
        int write_result = netfile_offset_write(data, offset, *length);
        if ((size_t)write_result == *length) {
            return TFTP_NO_ERROR;
        }
        if (write_result == -EBADF) {
            return TFTP_ERR_BAD_STATE;
        }
        return TFTP_ERR_IO;
    }
}

static void file_close(void* cookie) {
    file_info_t* file_info = cookie;
    if (file_info->type == netboot && file_info->netboot_file == NULL) {
        netfile_close();
    } else if (file_info->type == paver) {
        unsigned int refcount = atomic_fetch_sub(&file_info->paver.buf_refcount, 1);
        if (refcount == 1) {
            dealloc_paver_buffer(file_info);
        }
    }
}

static tftp_status transport_send(void* data, size_t len, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    zx_status_t status = udp6_send(data, len, &transport_info->dest_addr,
                                   transport_info->dest_port, NB_TFTP_OUTGOING_PORT, true);
    if (status != ZX_OK) {
        return TFTP_ERR_IO;
    }

    // The timeout is relative to sending instead of receiving a packet, since there are some
    // received packets we want to ignore (duplicate ACKs).
    if (transport_info->timeout_ms != 0) {
        tftp_next_timeout = zx_deadline_after(ZX_MSEC(transport_info->timeout_ms));
        update_timeouts();
    }
    return TFTP_NO_ERROR;
}

static int transport_timeout_set(uint32_t timeout_ms, void* transport_cookie) {
    transport_info_t* transport_info = transport_cookie;
    transport_info->timeout_ms = timeout_ms;
    return 0;
}

static void initialize_connection(const ip6_addr_t* saddr, uint16_t sport) {
    int ret = tftp_init(&session, tftp_session_scratch,
                        sizeof(tftp_session_scratch));
    if (ret != TFTP_NO_ERROR) {
        printf("netsvc: failed to initiate tftp session\n");
        session = NULL;
        return;
    }

    // Initialize file interface
    tftp_file_interface file_ifc = {file_open_read, file_open_write,
                                    file_read, file_write, file_close};
    tftp_session_set_file_interface(session, &file_ifc);

    // Initialize transport interface
    memcpy(&transport_info.dest_addr, saddr, sizeof(ip6_addr_t));
    transport_info.dest_port = sport;
    transport_info.timeout_ms = TFTP_TIMEOUT_SECS * 1000;
    tftp_transport_interface transport_ifc = {transport_send, NULL, transport_timeout_set};
    tftp_session_set_transport_interface(session, &transport_ifc);
}

static void end_connection(void) {
    session = NULL;
    tftp_next_timeout = ZX_TIME_INFINITE;
}

void tftp_timeout_expired(void) {
    tftp_status result = tftp_timeout(session, tftp_out_scratch, &last_msg_size,
                                      sizeof(tftp_out_scratch), &transport_info.timeout_ms,
                                      &file_info);
    if (result == TFTP_ERR_TIMED_OUT) {
        printf("netsvc: excessive timeouts, dropping tftp connection\n");
        file_close(&file_info);
        end_connection();
        netfile_abort_write();
    } else if (result < 0) {
        printf("netsvc: failed to generate timeout response, dropping tftp connection\n");
        file_close(&file_info);
        end_connection();
        netfile_abort_write();
    } else {
        if (last_msg_size > 0) {
            tftp_status send_result = transport_send(tftp_out_scratch, last_msg_size,
                                                     &transport_info);
            if (send_result != TFTP_NO_ERROR) {
                printf("netsvc: failed to send tftp timeout response (err = %d)\n", send_result);
            }
        }
    }
}

void tftp_recv(void* data, size_t len,
               const ip6_addr_t* daddr, uint16_t dport,
               const ip6_addr_t* saddr, uint16_t sport) {
    if (dport == NB_TFTP_INCOMING_PORT) {
        if (session != NULL) {
            printf("netsvc: only one simultaneous tftp session allowed\n");
            // ignore attempts to connect when a session is in progress
            return;
        }
        initialize_connection(saddr, sport);
    } else if (!session) {
        // Ignore anything sent to the outgoing port unless we've already
        // established a connection.
        return;
    }

    last_msg_size = sizeof(tftp_out_scratch);

    char err_msg[128];
    tftp_handler_opts handler_opts = {.inbuf = data,
                                      .inbuf_sz = len,
                                      .outbuf = tftp_out_scratch,
                                      .outbuf_sz = &last_msg_size,
                                      .err_msg = err_msg,
                                      .err_msg_sz = sizeof(err_msg)};
    tftp_status status = tftp_handle_msg(session, &transport_info, &file_info,
                                         &handler_opts);
    switch (status) {
    case TFTP_NO_ERROR:
        return;
    case TFTP_TRANSFER_COMPLETED:
        printf("netsvc: tftp %s of file %s completed\n",
               file_info.is_write ? "write" : "read",
               file_info.filename);
        break;
    case TFTP_ERR_SHOULD_WAIT:
        break;
    default:
        printf("netsvc: %s\n", err_msg);
        netfile_abort_write();
        file_close(&file_info);
        break;
    }
    end_connection();
}

bool tftp_has_pending(void) {
    return session && tftp_session_has_pending(session);
}

void tftp_send_next(void) {
    last_msg_size = sizeof(tftp_out_scratch);
    tftp_prepare_data(session, tftp_out_scratch, &last_msg_size, &transport_info.timeout_ms,
                      &file_info);
    if (last_msg_size) {
        transport_send(tftp_out_scratch, last_msg_size, &transport_info);
    }
}
