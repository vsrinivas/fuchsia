// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <lib/log/log.h>
#include <lib/mtd/mtd-interface.h>

// Some chips report a spare size that is not capable of being read and/or
// written usually due to reserved bits for ECC or limits set by a NAND
// controller. Allow the spare size to be set based on a build flag to account
// for this.
#ifndef SPARE_SIZE
#define SPARE_SIZE 0
#endif

namespace mtd {

std::unique_ptr<MtdInterface> MtdInterface::Create(const std::string& path) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  if (!fd) {
    LOGF(ERROR, "MtdInterface")("Failed to open %s: %s\n", path.c_str(), strerror(errno));
    return nullptr;
  }

  mtd_info_t mtd_info;
  int ret = ioctl(fd.get(), MEMGETINFO, &mtd_info);
  if (ret) {
    LOGF(ERROR, "MtdInterface")("Failed to get info for %s: %s", path.c_str(), strerror(errno));
    return nullptr;
  }

  // Cannot use make_unique with a private constructor, so create MtdInterface
  // using new explicitly and wrap it in a unique_ptr immediately.
  return std::unique_ptr<MtdInterface>(new MtdInterface(std::move(fd), mtd_info));
}

MtdInterface::MtdInterface(fbl::unique_fd fd, const mtd_info_t& mtd_info)
    : fd_(std::move(fd)), mtd_info_(mtd_info) {}

uint32_t MtdInterface::PageSize() { return mtd_info_.writesize; }

uint32_t MtdInterface::BlockSize() { return mtd_info_.erasesize; }

uint32_t MtdInterface::OobSize() { return SPARE_SIZE > 0 ? SPARE_SIZE : mtd_info_.oobsize; }

uint32_t MtdInterface::Size() { return mtd_info_.size; }

zx_status_t MtdInterface::ReadPage(uint32_t byte_offset, void* data_bytes, uint32_t* actual) {
  if (byte_offset % PageSize() != 0) {
    LOG(ERROR, "MtdInterface")("byte_offset must be set to the start of a page");
    return ZX_ERR_INVALID_ARGS;
  }

  lseek(fd_.get(), byte_offset, SEEK_SET);

  ssize_t ret = read(fd_.get(), data_bytes, PageSize());
  if (ret != PageSize()) {
    LOGF(ERROR, "MtdInterface")
    ("Failed to read page at offset %u: %s", byte_offset, strerror(errno));
    // TODO(mbrunson): Return more specific error.
    return ZX_ERR_IO;
  }

  *actual = static_cast<uint32_t>(ret);
  return ZX_OK;
}

zx_status_t MtdInterface::ReadOob(uint32_t byte_offset, void* oob_bytes) {
  if (byte_offset % PageSize() != 0) {
    LOG(ERROR, "MtdInterface")("byte_offset must be set to the start of a page.");
    return ZX_ERR_INVALID_ARGS;
  }

  struct mtd_oob_buf oob = {byte_offset, OobSize(), static_cast<unsigned char*>(oob_bytes)};

  int ret = ioctl(fd_.get(), MEMREADOOB, &oob);
  if (ret < 0) {
    LOGF(ERROR, "MtdInterface")
    ("Failed to read OOB at offset %u: %s", byte_offset, strerror(errno));
    // TODO(mbrunson): Return more specific error.
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t MtdInterface::WritePage(uint32_t byte_offset, const void* data_bytes,
                                    const void* oob_bytes) {
  if (byte_offset % PageSize() != 0) {
    LOG(ERROR, "MtdInterface")("byte_offset must be set to the start of a page.");
    return ZX_ERR_INVALID_ARGS;
  }

  ssize_t ret;

  // Some drivers don't support MEMWRITE so implement a fallback using
  // MEMWRITEOOB and POSIX write operations. Incidentally, tests on using
  // nandsim fail in this manner and do not report the failure, so we're relying
  // on a build flag.
#ifdef MEMWRITE_NOT_SUPPORTED
  if (oob_bytes) {
    struct mtd_oob_buf oob_req = {byte_offset, OobSize(),
                                  reinterpret_cast<unsigned char*>(const_cast<void*>(oob_bytes))};

    ret = ioctl(fd_.get(), MEMWRITEOOB, &oob_req);
    if (ret < 0) {
      LOGF(ERROR, "MtdInterface")
      ("Failed to write page at offset %d: %s", byte_offset, strerror(errno));
      return ZX_ERR_IO;
    }
  }

  if (data_bytes) {
    if (lseek(fd_.get(), byte_offset, SEEK_SET) != byte_offset) {
      LOGF(ERROR, "MtdInterface")("Failed to seek to offset %d: %s", byte_offset, strerror(errno));
      return ZX_ERR_IO;
    }

    ret = write(fd_.get(), data_bytes, PageSize());
    LOGF(INFO, "MtdInterface")("write: %d", ret);
    if (ret != PageSize()) {
      LOGF(ERROR, "MtdInterface")
      ("Failed to write page at offset %d: %s", byte_offset, strerror(errno));
      return ZX_ERR_IO;
    }

    if (static_cast<size_t>(ret) != PageSize()) {
      LOGF(ERROR, "MtdInterface")
      ("Wrote unexpected number of bytes. Expected %lu, wrote %d: %s", PageSize(), ret,
       strerror(errno));
      return ZX_ERR_IO_DATA_LOSS;
    }
  }
#else
  struct mtd_write_req req = {static_cast<uint64_t>(byte_offset),
                              static_cast<uint64_t>(PageSize()),
                              static_cast<uint64_t>(OobSize()),
                              reinterpret_cast<uint64_t>(data_bytes),
                              reinterpret_cast<uint64_t>(oob_bytes),
                              MTD_OPS_PLACE_OOB,
                              {0}};

  ret = ioctl(fd_.get(), MEMWRITE, &req);
  if (ret < 0) {
    LOGF(ERROR, "MtdInterface")
    ("Failed to write page at offset %d: %s", byte_offset, strerror(errno));
    // TODO(mbrunson): Return more specific error.
    return ZX_ERR_IO;
  }
#endif

  return ZX_OK;
}

zx_status_t MtdInterface::EraseBlock(uint32_t byte_offset) {
  if (byte_offset % BlockSize() != 0) {
    LOG(ERROR, "MtdInterface")("byte_offset must be set to the start of a block.");
    return ZX_ERR_INVALID_ARGS;
  }

  int ret;
  erase_info_t ei = {byte_offset, BlockSize()};

  if ((ret = ioctl(fd_.get(), MEMERASE, &ei)) < 0) {
    perror("Failed to erase block");
    // TODO(mbrunson): Return more specific error.
    return ZX_ERR_IO;
  }
  return ZX_OK;
}

zx_status_t MtdInterface::IsBadBlock(uint32_t byte_offset, bool* is_bad_block) {
  if (byte_offset % BlockSize() != 0) {
    LOG(ERROR, "MtdInterface")("byte_offset must be set to the start of a block.");
    return ZX_ERR_INVALID_ARGS;
  }

  loff_t seek = byte_offset;

  int ret = ioctl(fd_.get(), MEMGETBADBLOCK, &seek);
  if (ret < 0) {
    LOGF(ERROR, "MtdInterface")
    ("Failed to get bad block info at offset %d: %s", byte_offset, strerror(errno));
    // TODO(mbrunson): Return more specific error.
    return ZX_ERR_IO;
  }

  *is_bad_block = ret > 0;
  return ZX_OK;
}

}  // namespace mtd
