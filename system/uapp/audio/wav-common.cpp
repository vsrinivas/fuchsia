// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <magenta/assert.h>
#include <fbl/auto_call.h>
#include <fbl/algorithm.h>
#include <mxio/io.h>
#include <stdio.h>

#include "wav-source.h"

constexpr uint32_t WAVCommon::RIFF_FOUR_CC;
constexpr uint32_t WAVCommon::WAVE_FOUR_CC;
constexpr uint32_t WAVCommon::FMT_FOUR_CC;
constexpr uint32_t WAVCommon::DATA_FOUR_CC;

constexpr uint16_t WAVCommon::FORMAT_UNKNOWN;
constexpr uint16_t WAVCommon::FORMAT_LPCM;
constexpr uint16_t WAVCommon::FORMAT_MSFT_ADPCM;
constexpr uint16_t WAVCommon::FORMAT_IEEE_FLOAT;
constexpr uint16_t WAVCommon::FORMAT_MSFT_ALAW;
constexpr uint16_t WAVCommon::FORMAT_MSFT_MULAW;

mx_status_t WAVCommon::Initialize(const char* filename, InitMode mode) {
    if (fd_ >= 0) {
        printf("Failed to initialize WAVCommon for \"%s\", already initialized\n", filename);
        return MX_ERR_BAD_STATE;
    }

    int flags = (mode == InitMode::SOURCE)
              ? O_RDONLY
              : O_RDWR | O_CREAT;
    fd_ = ::open(filename, flags);
    if (fd_ < 0) {
        printf("Failed to open \"%s\" (res %d)\n", filename, fd_);
        return static_cast<mx_status_t>(fd_);
    }

    return MX_OK;
}

void WAVCommon::Close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

mx_status_t WAVCommon::Read(void* buf, size_t len) {
    if (fd_ < 0) return MX_ERR_BAD_STATE;

    ssize_t res = ::read(fd_, buf, len);
    if (res < 0) {
        printf("Read error (errno %d)\n", errno);
        return static_cast<mx_status_t>(errno);
    }

    if (static_cast<size_t>(res) != len) {
        printf("Short read error (%zd < %zu)\n", res, len);
        return MX_ERR_IO;
    }

    return MX_OK;
}

mx_status_t WAVCommon::Write(const void* buf, size_t len) {
    if (fd_ < 0) return MX_ERR_BAD_STATE;

    ssize_t res = ::write(fd_, buf, len);
    if (res < 0) {
        printf("Write error (errno %d)\n", errno);
        return static_cast<mx_status_t>(errno);
    }

    if (static_cast<size_t>(res) != len) {
        printf("Short write error (%zd < %zu)\n", res, len);
        return MX_ERR_IO;
    }

    return MX_OK;
}

mx_status_t WAVCommon::Seek(off_t abs_pos) {
    if (fd_ < 0) return MX_ERR_BAD_STATE;
    if (abs_pos < 0) return MX_ERR_INVALID_ARGS;

    off_t res = ::lseek(fd_, abs_pos, SEEK_SET);
    if (res < 0) {
        printf("Seek error (errno %d)\n", errno);
        return static_cast<mx_status_t>(errno);
    }

    if (res != abs_pos) {
        printf("Failed to see to target (target %zd, got %zd)\n",
                static_cast<ssize_t>(abs_pos),
                static_cast<ssize_t>(res));
        return MX_ERR_IO;
    }

    return MX_OK;
}
