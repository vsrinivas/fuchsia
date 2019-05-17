// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "paver.h"

#include <algorithm>

#include <fbl/auto_call.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/directory.h>
#include <lib/zx/time.h>
#include <zircon/boot/netboot.h>

#include "payload-streamer.h"

namespace netsvc {
namespace {

size_t NB_IMAGE_PREFIX_LEN() {
    return strlen(NB_IMAGE_PREFIX);
}
size_t NB_FILENAME_PREFIX_LEN() {
    return strlen(NB_FILENAME_PREFIX);
}

} // namespace

Paver* Paver::Get() {
    static Paver* instance_ = nullptr;
    if (instance_ == nullptr) {
        zx::channel local, remote;
        auto status = zx::channel::create(0, &local, &remote);
        if (status != ZX_OK) {
            return nullptr;
        }
        status = fdio_service_connect("/svc", remote.release());
        if (status != ZX_OK) {
            return nullptr;
        }
        instance_ = new Paver(std::move(local));
    }
    return instance_;
}

bool Paver::InProgress() {
    return in_progress_.load();
}
zx_status_t Paver::exit_code() {
    return exit_code_.load();
}
void Paver::reset_exit_code() {
    exit_code_.store(ZX_OK);
}

int Paver::StreamBuffer() {
  zx::time last_reported = zx::clock::get_monotonic();
    int result = 0;
    auto callback = [this, &last_reported, &result](void* buf, size_t read_offset,
                                                    size_t size, size_t* actual) {
        if (read_offset >= size_) {
            *actual = 0;
            return ZX_OK;
        }
        sync_completion_reset(&data_ready_);
        size_t write_offset = write_offset_.load();
        while (write_offset == read_offset) {
            // Wait for more data to be written -- we are allowed up to 3 tftp timeouts before
            // a connection is dropped, so we should wait at least that long before giving up.
            auto status = sync_completion_wait(&data_ready_, timeout_.get());
            if (status != ZX_OK) {
                printf("netsvc: timed out while waiting for data in paver-copy thread\n");
                exit_code_.store(status);
                result = TFTP_ERR_TIMED_OUT;
                return ZX_ERR_TIMED_OUT;
            }
            sync_completion_reset(&data_ready_);
            write_offset = write_offset_.load();
        };
        size = std::min(size, write_offset - read_offset);
        memcpy(buf, buffer() + read_offset, size);
        *actual = size;
        zx::time curr_time = zx::clock::get_monotonic();
        if (curr_time - last_reported >= zx::sec(1)) {
            float complete =
                (static_cast<float>(read_offset) / static_cast<float>(size_)) *
                100.f;
            printf("netsvc: paver write progress %0.1f%%\n", complete);
            last_reported = curr_time;
        }
        return ZX_OK;
    };

    fbl::AutoCall cleanup([this, &result]() {
        unsigned int refcount = std::atomic_fetch_sub(&buf_refcount_, 1u);
        if (refcount == 1) {
            buffer_mapper_.Reset();
        }

        paver_svc_.reset();

        if (result != 0) {
            printf("netsvc: copy exited prematurely (%d): expect paver errors\n", result);
        }

        in_progress_.store(false);
    });

    zx::channel client, server;
    auto status = zx::channel::create(0, &client, &server);
    if (status) {
        fprintf(stderr, "netsvc: unable to create channel\n");
        exit_code_.store(status);
        return 0;
    }

    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    PayloadStreamer streamer(std::move(server), std::move(callback));
    loop.StartThread("payload-streamer");

    // Blocks untils paving is complete.
    auto io_status = fuchsia_paver_PaverWriteVolumes(paver_svc_.get(), client.release(),
                                                     &status);
    status = io_status == ZX_OK ? status : io_status;
    exit_code_.store(status);

    return 0;
}

int Paver::MonitorBuffer() {
    int result = TFTP_NO_ERROR;

    fbl::AutoCall cleanup([this, &result]() {
        unsigned int refcount = std::atomic_fetch_sub(&buf_refcount_, 1u);
        if (refcount == 1) {
            buffer_mapper_.Reset();
        }

        paver_svc_.reset();

        if (result != 0) {
            printf("netsvc: copy exited prematurely (%d): expect paver errors\n", result);
        }

        in_progress_.store(false);
    });

    size_t write_ndx = 0;
    do {
        // Wait for more data to be written -- we are allowed up to 3 tftp timeouts before
        // a connection is dropped, so we should wait at least that long before giving up.
        auto status = sync_completion_wait(&data_ready_, timeout_.get());
        if (status != ZX_OK) {
            printf("netsvc: timed out while waiting for data in paver-copy thread\n");
            exit_code_.store(status);
            result = TFTP_ERR_TIMED_OUT;
            return result;
        }
        sync_completion_reset(&data_ready_);
        write_ndx = write_offset_.load();
    } while (write_ndx < size_);

    zx::vmo dup;
    auto status = buffer_mapper_.vmo().duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
    if (status != ZX_OK) {
        exit_code_.store(status);
        return 0;
    }

    fuchsia_mem_Buffer buffer = {
        .vmo = dup.release(),
        .size = buffer_mapper_.size(),
    };

    zx_status_t io_status = ZX_ERR_INTERNAL;
    // Blocks untils paving is complete.
    switch (command_) {
    case Command::kDataFile:
        io_status = fuchsia_paver_PaverWriteDataFile(paver_svc_.get(), path_, strlen(path_),
                                                     &buffer, &status);
        break;
    case Command::kBootloader:
        io_status = fuchsia_paver_PaverWriteBootloader(paver_svc_.get(), &buffer, &status);
        break;
    case Command::kAsset:
        io_status = fuchsia_paver_PaverWriteAsset(paver_svc_.get(), configuration_, asset_, &buffer,
                                                  &status);
        break;
    default:
        io_status = ZX_OK;
        result = TFTP_ERR_INTERNAL;
        status = ZX_ERR_INTERNAL;
        break;
    }
    status = io_status == ZX_OK ? status : io_status;
    exit_code_.store(status);

    return 0;
}

tftp_status Paver::OpenWrite(const char* filename, size_t size) {
    // Paving an image to disk.
    if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_FVM_HOST_FILENAME)) {
        printf("netsvc: Running FVM Paver\n");
        command_ = Command::kFvm;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_BOOTLOADER_HOST_FILENAME)) {
        printf("netsvc: Running BOOTLOADER Paver\n");
        command_ = Command::kBootloader;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_ZIRCONA_HOST_FILENAME)) {
        printf("netsvc: Running ZIRCON-A Paver\n");
        command_ = Command::kAsset;
        configuration_ = fuchsia_paver_Configuration_A;
        asset_ = fuchsia_paver_Asset_KERNEL;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_ZIRCONB_HOST_FILENAME)) {
        printf("netsvc: Running ZIRCON-B Paver\n");
        command_ = Command::kAsset;
        configuration_ = fuchsia_paver_Configuration_B;
        asset_ = fuchsia_paver_Asset_KERNEL;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_ZIRCONR_HOST_FILENAME)) {
        printf("netsvc: Running ZIRCON-R Paver\n");
        command_ = Command::kAsset;
        configuration_ = fuchsia_paver_Configuration_RECOVERY;
        asset_ = fuchsia_paver_Asset_KERNEL;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_VBMETAA_HOST_FILENAME)) {
        printf("netsvc: Running VBMETA-A Paver\n");
        command_ = Command::kAsset;
        configuration_ = fuchsia_paver_Configuration_A;
        asset_ = fuchsia_paver_Asset_VERIFIED_BOOT_METADATA;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_VBMETAB_HOST_FILENAME)) {
        printf("netsvc: Running VBMETA-B Paver\n");
        command_ = Command::kAsset;
        configuration_ = fuchsia_paver_Configuration_B;
        asset_ = fuchsia_paver_Asset_VERIFIED_BOOT_METADATA;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_VBMETAR_HOST_FILENAME)) {
        printf("netsvc: Running VBMETA-R Paver\n");
        command_ = Command::kAsset;
        configuration_ = fuchsia_paver_Configuration_RECOVERY;
        asset_ = fuchsia_paver_Asset_VERIFIED_BOOT_METADATA;
    } else if (!strcmp(filename + NB_IMAGE_PREFIX_LEN(), NB_SSHAUTH_HOST_FILENAME)) {
        printf("netsvc: Installing SSH authorized_keys\n");
        command_ = Command::kDataFile;
        strncpy(path_, "ssh/authorized_keys", PATH_MAX);
    } else {
        fprintf(stderr, "netsvc: Unknown Paver\n");
        return TFTP_ERR_IO;
    }

    auto status = buffer_mapper_.CreateAndMap(size, "paver");
    if (status != ZX_OK) {
        printf("netsvc: unable to allocate and map buffer\n");
        return status;
    }
    fbl::AutoCall buffer_cleanup([this]() { buffer_mapper_.Reset(); });

    zx::channel paver_local, paver_remote;
    status = zx::channel::create(0, &paver_local, &paver_remote);
    if (status != ZX_OK) {
        fprintf(stderr, "netsvc: Unable to create channel pair.\n");
        return TFTP_ERR_IO;
    }
    status = fdio_service_connect_at(svc_root_.get(), fuchsia_paver_Paver_Name,
                                     paver_remote.release());
    if (status != ZX_OK) {
        fprintf(stderr, "netsvc: Unable to open /svc/%s.\n", fuchsia_paver_Paver_Name);
        return TFTP_ERR_IO;
    }

    paver_svc_ = std::move(paver_local);
    fbl::AutoCall svc_cleanup([&]() { paver_svc_.reset(); });

    size_ = size;

    buf_refcount_.store(2u);
    write_offset_.store(0ul);
    exit_code_.store(0);
    in_progress_.store(true);

    sync_completion_reset(&data_ready_);

    auto thread_fn = command_ == Command::kFvm
                         ? [](void* arg) { return static_cast<Paver*>(arg)->StreamBuffer(); }
                         : [](void* arg) { return static_cast<Paver*>(arg)->MonitorBuffer(); };
    if (thrd_create(&buf_thrd_, thread_fn, this) != thrd_success) {
        fprintf(stderr, "netsvc: unable to launch buffer stream/monitor thread\n");
        status = ZX_ERR_NO_RESOURCES;
        return status;
    }
    thrd_detach(buf_thrd_);
    svc_cleanup.cancel();
    buffer_cleanup.cancel();

    return TFTP_NO_ERROR;
}

tftp_status Paver::Write(const void* data, size_t* length, off_t offset) {
    if (!InProgress()) {
        printf("netsvc: paver exited prematurely with %d\n", exit_code());
        reset_exit_code();
        return TFTP_ERR_IO;
    }

    if ((static_cast<size_t>(offset) > size_) ||
        (offset + *length) > size_) {
        return TFTP_ERR_INVALID_ARGS;
    }
    memcpy(&buffer()[offset], data, *length);
    size_t new_offset = offset + *length;
    write_offset_.store(new_offset);
    // Wake the paver thread, if it is waiting for data
    sync_completion_signal(&data_ready_);
    return TFTP_NO_ERROR;
}

void Paver::Close() {
    unsigned int refcount = std::atomic_fetch_sub(&buf_refcount_, 1u);
    if (refcount == 1) {
        buffer_mapper_.Reset();
    }
    // TODO: Signal thread to wake up rather than wait for it to timeout if
    // stream is closed before write is complete?
}

} // namespace netsvc
