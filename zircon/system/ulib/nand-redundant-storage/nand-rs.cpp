// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>

#include <lib/cksum.h>
#include <lib/log/log.h>
#include <lib/mtd/nand-interface.h>
#include <lib/nand-redundant-storage/nand-redundant-storage.h>
#include <zircon/assert.h>

namespace nand_rs {

namespace {

constexpr const char kNandRsMagic[] = "ZNND";
constexpr uint32_t kNandRsMagicSize = sizeof(kNandRsMagic) - 1;

struct NandRsHeader {
    char magic[kNandRsMagicSize];
    // CRC-32 of the file contents.
    uint32_t crc;
    // Size of the file.
    uint32_t file_size;
};

constexpr uint32_t kNandRsHeaderSize = sizeof(NandRsHeader);

static_assert(kNandRsHeaderSize == 3 * sizeof(uint32_t));

std::unique_ptr<NandRsHeader> MakeNandRsHeader(const std::vector<uint8_t>& buf) {
    std::unique_ptr<NandRsHeader> header(std::make_unique<NandRsHeader>());
    memcpy(header->magic, kNandRsMagic, kNandRsMagicSize);
    header->crc = crc32(0, buf.data(), buf.size());
    header->file_size = static_cast<uint32_t>(buf.size());
    return header;
}

// Helper for ReadMtdToBuffer that attempts to read a file from a block.
//
// Returns the file size if a file could be read and verified, or a negative
// number otherwise.
int ReadToBufferHelper(const std::unique_ptr<mtd::NandInterface>& nand,
                       std::vector<uint8_t>* block_buffer,
                       uint32_t mtd_offset) {
    // Reads in the whole block for simplification.
    for (uint32_t block_offset = 0; block_offset < nand->BlockSize();
         block_offset += nand->PageSize()) {
        uint32_t actual_bytes_read = 0;
        zx_status_t res =
            nand->ReadPage(
                mtd_offset + block_offset,
                block_buffer->data() + block_offset,
                &actual_bytes_read);
        if (res != ZX_OK || actual_bytes_read != nand->PageSize()) {
            fprintf(stderr, "Unable to read page at offset %d: %s\n", 0, strerror(errno));
            return -1;
        }
    }
    NandRsHeader* header = reinterpret_cast<NandRsHeader*>(block_buffer->data());
    if (strncmp(header->magic, kNandRsMagic, kNandRsMagicSize) != 0) {
        return -1;
    }
    if (header->file_size == 0 || header->file_size > nand->BlockSize() - kNandRsHeaderSize) {
        fprintf(stderr, "File size in block at offset %d invalid: %d\n",
                mtd_offset, header->file_size);
        return -1;
    }

    uint32_t file_checksum = crc32(0, block_buffer->data() + kNandRsHeaderSize, header->file_size);
    if (file_checksum != header->crc) {
        fprintf(stderr, "File checksum %d does not match stored checksum %d "
                        "in block %d\n",
                file_checksum, header->crc, mtd_offset);
        return -1;
    }
    return header->file_size;
}

} // namespace

std::unique_ptr<NandRedundantStorage> NandRedundantStorage::Create(
    std::unique_ptr<mtd::NandInterface> iface) {

    if (!iface) {
        return nullptr;
    }
    // Can't use std::make_unique due to private constructor.
    return std::unique_ptr<NandRedundantStorage>(new NandRedundantStorage(std::move(iface)));
}

NandRedundantStorage::NandRedundantStorage(std::unique_ptr<mtd::NandInterface> iface)
    : iface_(std::move(iface)) {}

zx_status_t NandRedundantStorage::WriteBuffer(
    const std::vector<uint8_t>& buffer,
    uint32_t num_copies,
    uint32_t* num_copies_written) {

    ZX_DEBUG_ASSERT(iface_);
    ZX_ASSERT(num_copies_written);
    ZX_ASSERT(num_copies != 0);
    ZX_ASSERT(!buffer.empty());
    ZX_ASSERT_MSG(buffer.size() <= iface_->BlockSize() - kNandRsHeaderSize, "File size too large");
    ZX_ASSERT_MSG(num_copies * iface_->BlockSize() <= iface_->Size(),
                  "Not enough space for %d copies", num_copies);

    *num_copies_written = 0;

    // Allocates a full block for ease of writing. If the buffer-to-be-copied
    // crosses a page boundary, this will allow for padding of zeroes without
    // additional logic.
    std::vector<uint8_t> block_buffer(iface_->BlockSize(), 0);
    auto header = MakeNandRsHeader(buffer);
    memcpy(block_buffer.data(), header.get(), sizeof(*header.get()));

    // Writes the file.
    memcpy(block_buffer.data() + sizeof(*header.get()), buffer.data(), buffer.size());

    for (uint32_t i = 0; i < num_copies; ++i) {
        uint32_t byte_offset = i * iface_->BlockSize();
        // This case can happen if there are a very large number of copies to
        // write, but is quite unlikely to happen. This scenario is outlined
        // in the header, as it is the caller's decision what to do about this.
        if (byte_offset >= iface_->Size()) {
            fprintf(stderr, "Reached end of MTD device without writing all copies\n");
            return *num_copies_written > 0 ? ZX_OK : ZX_ERR_NO_SPACE;
        }

        // Skip this block if:
        //
        // -- It's not possible to determine if the block is bad.
        // -- The block is explicitly marked as bad.
        // -- We are unable to erase the block.
        bool is_bad_block;
        if (iface_->IsBadBlock(byte_offset, &is_bad_block) != ZX_OK ||
            is_bad_block ||
            iface_->EraseBlock(byte_offset) != ZX_OK) {
            ++num_copies;
            continue;
        }

        // If the buffer crosses a page boundary, continue writing each section
        // of the buffer, padding with zeroes until the next page boundary is
        // reached.
        bool buffer_written = true;
        for (uint32_t buffer_bytes_written = 0;
             buffer_bytes_written < buffer.size() + kNandRsHeaderSize;
             buffer_bytes_written += iface_->PageSize()) {
            if (iface_->WritePage(
                    byte_offset,
                    block_buffer.data() + buffer_bytes_written, nullptr) != ZX_OK) {
                ++num_copies;
                buffer_written = false;
                break;
            }

            // Still need to update byte offset for writing the remainder of the
            // file to this block.
            byte_offset += iface_->PageSize();
        }

        if (buffer_written) {
            (*num_copies_written)++;
        }
    }
    return ZX_OK;
}

zx_status_t NandRedundantStorage::ReadToBuffer(std::vector<uint8_t>* out_buffer) {
    ZX_DEBUG_ASSERT(iface_);
    ZX_ASSERT(out_buffer);
    std::vector<uint8_t> block_buffer(iface_->BlockSize(), 0);
    for (uint32_t offset = 0; offset < iface_->Size();
         offset += iface_->BlockSize()) {
        bool is_bad_block;
        zx_status_t bad_block_status = iface_->IsBadBlock(offset, &is_bad_block);
        if (bad_block_status != ZX_OK) {
            fprintf(stderr, "Error reading block status at offset %d: %s\n",
                    offset, strerror(errno));
            return bad_block_status;
        }
        if (is_bad_block) {
            continue;
        }

        int file_size = ReadToBufferHelper(iface_, &block_buffer, offset);
        if (file_size < 0) {
            continue;
        }
        out_buffer->resize(file_size, 0);
        memcpy(out_buffer->data(), block_buffer.data() + kNandRsHeaderSize, file_size);
        return ZX_OK;
    }

    fprintf(stderr, "No valid files found.\n");
    return ZX_ERR_IO;
}

} // namespace nand_rs
