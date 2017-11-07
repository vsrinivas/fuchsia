// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo-file.h>

namespace fs {

PseudoFile::PseudoFile(ReadHandler read_handler, WriteHandler write_handler)
    : read_handler_(fbl::move(read_handler)),
      write_handler_(fbl::move(write_handler)) {
}

PseudoFile::~PseudoFile() = default;

zx_status_t PseudoFile::ValidateFlags(uint32_t flags) {
    if (flags & ZX_FS_FLAG_DIRECTORY) {
        return ZX_ERR_NOT_DIR;
    }
    if (IsReadable(flags) && !read_handler_) {
        return ZX_ERR_ACCESS_DENIED;
    }
    if (IsWritable(flags) && !write_handler_) {
        return ZX_ERR_ACCESS_DENIED;
    }
    return ZX_OK;
}

zx_status_t PseudoFile::Getattr(vnattr_t* attr) {
    memset(attr, 0, sizeof(vnattr_t));
    attr->mode = V_TYPE_FILE;
    if (read_handler_)
        attr->mode |= V_IRUSR;
    if (write_handler_)
        attr->mode |= V_IWUSR;
    attr->nlink = 1;
    return ZX_OK;
}

BufferedPseudoFile::BufferedPseudoFile(ReadHandler read_handler, WriteHandler write_handler,
                                       size_t input_buffer_capacity)
    : PseudoFile(fbl::move(read_handler), fbl::move(write_handler)),
      input_buffer_capacity_(input_buffer_capacity) {}

BufferedPseudoFile::~BufferedPseudoFile() = default;

zx_status_t BufferedPseudoFile::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    fbl::String output;
    if (IsReadable(flags)) {
        zx_status_t status = read_handler_(&output);
        if (status != ZX_OK) {
            return status;
        }
    }

    *out_redirect = fbl::AdoptRef(new Content(fbl::WrapRefPtr(this), flags, fbl::move(output)));
    return ZX_OK;
}

BufferedPseudoFile::Content::Content(fbl::RefPtr<BufferedPseudoFile> file, uint32_t flags,
                                     fbl::String output)
    : file_(fbl::move(file)), flags_(flags), output_(fbl::move(output)) {}

BufferedPseudoFile::Content::~Content() {
    delete[] input_data_;
}

zx_status_t BufferedPseudoFile::Content::ValidateFlags(uint32_t flags) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t BufferedPseudoFile::Content::Close() {
    if (IsWritable(flags_)) {
        return file_->write_handler_(fbl::StringPiece(input_data_, input_length_));
    }
    return ZX_OK;
}

zx_status_t BufferedPseudoFile::Content::Getattr(vnattr_t* a) {
    return file_->Getattr(a);
}

zx_status_t BufferedPseudoFile::Content::Read(void* data, size_t length, size_t offset,
                                              size_t* out_actual) {
    ZX_DEBUG_ASSERT(IsReadable(flags_));

    if (length == 0u || offset >= output_.length()) {
        *out_actual = 0u;
        return ZX_OK;
    }
    size_t remaining_length = output_.length() - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    memcpy(data, output_.data() + offset, length);
    *out_actual = length;
    return ZX_OK;
}

zx_status_t BufferedPseudoFile::Content::Write(const void* data, size_t length, size_t offset,
                                               size_t* out_actual) {
    ZX_DEBUG_ASSERT(IsWritable(flags_));

    if (length == 0u) {
        *out_actual = 0u;
        return ZX_OK;
    }
    if (offset >= file_->input_buffer_capacity_) {
        return ZX_ERR_NO_SPACE;
    }

    size_t remaining_length = file_->input_buffer_capacity_ - offset;
    if (length > remaining_length) {
        length = remaining_length;
    }
    if (offset + length > input_length_) {
        SetInputLength(offset + length);
    }
    memcpy(input_data_ + offset, data, length);
    *out_actual = length;
    return ZX_OK;
}

zx_status_t BufferedPseudoFile::Content::Append(const void* data, size_t length, size_t* out_end,
                                                size_t* out_actual) {
    ZX_DEBUG_ASSERT(IsWritable(flags_));

    zx_status_t status = Write(data, length, input_length_, out_actual);
    if (status == ZX_OK) {
        *out_end = input_length_;
    }
    return status;
}

zx_status_t BufferedPseudoFile::Content::Truncate(size_t length) {
    ZX_DEBUG_ASSERT(IsWritable(flags_));

    if (length > file_->input_buffer_capacity_) {
        return ZX_ERR_NO_SPACE;
    }

    size_t old_length = input_length_;
    SetInputLength(length);
    if (length > old_length) {
        memset(input_data_ + old_length, 0, length - old_length);
    }
    return ZX_OK;
}

void BufferedPseudoFile::Content::SetInputLength(size_t length) {
    ZX_DEBUG_ASSERT(length <= file_->input_buffer_capacity_);

    if (input_data_ == nullptr && length != 0u) {
        input_data_ = new char[file_->input_buffer_capacity_];
    }
    input_length_ = length;
}

UnbufferedPseudoFile::UnbufferedPseudoFile(ReadHandler read_handler, WriteHandler write_handler)
    : PseudoFile(fbl::move(read_handler), fbl::move(write_handler)) {}

UnbufferedPseudoFile::~UnbufferedPseudoFile() = default;

zx_status_t UnbufferedPseudoFile::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    *out_redirect = fbl::AdoptRef(new Content(fbl::WrapRefPtr(this), flags));
    return ZX_OK;
}

UnbufferedPseudoFile::Content::Content(fbl::RefPtr<UnbufferedPseudoFile> file, uint32_t flags)
    : file_(fbl::move(file)), flags_(flags),
      truncated_since_last_successful_write_(flags_ & (ZX_FS_FLAG_CREATE | ZX_FS_FLAG_TRUNCATE)) {}

UnbufferedPseudoFile::Content::~Content() = default;

zx_status_t UnbufferedPseudoFile::Content::Open(uint32_t flags, fbl::RefPtr<Vnode>* out_redirect) {
    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UnbufferedPseudoFile::Content::Close() {
    if (IsWritable(flags_) && truncated_since_last_successful_write_) {
        return file_->write_handler_(fbl::StringPiece());
    }
    return ZX_OK;
}

zx_status_t UnbufferedPseudoFile::Content::Getattr(vnattr_t* a) {
    return file_->Getattr(a);
}

zx_status_t UnbufferedPseudoFile::Content::Read(void* data, size_t length, size_t offset,
                                                size_t* out_actual) {
    ZX_DEBUG_ASSERT(IsReadable(flags_));

    if (offset != 0u) {
        // If the offset is non-zero, we assume the client already read the property.
        // Simulate end of file.
        *out_actual = 0u;
        return ZX_OK;
    }

    fbl::String output;
    zx_status_t status = file_->read_handler_(&output);
    if (status == ZX_OK) {
        if (length > output.length()) {
            length = output.length();
        }
        memcpy(data, output.data(), length);
        *out_actual = length;
    }
    return status;
}

zx_status_t UnbufferedPseudoFile::Content::Write(const void* data, size_t length, size_t offset,
                                                 size_t* out_actual) {
    ZX_DEBUG_ASSERT(IsWritable(flags_));

    if (offset != 0u) {
        // If the offset is non-zero, we assume the client already wrote the property.
        // Simulate an inability to write additional data.
        return ZX_ERR_NO_SPACE;
    }

    zx_status_t status = file_->write_handler_(
        fbl::StringPiece(static_cast<const char*>(data), length));
    if (status == ZX_OK) {
        truncated_since_last_successful_write_ = false;
        *out_actual = length;
    }
    return status;
}

zx_status_t UnbufferedPseudoFile::Content::Append(const void* data, size_t length, size_t* out_end,
                                                  size_t* out_actual) {
    ZX_DEBUG_ASSERT(IsWritable(flags_));

    zx_status_t status = Write(data, length, 0u, out_actual);
    if (status == ZX_OK) {
        *out_end = length;
    }
    return status;
}

zx_status_t UnbufferedPseudoFile::Content::Truncate(size_t length) {
    ZX_DEBUG_ASSERT(IsWritable(flags_));

    if (length != 0u) {
        return ZX_ERR_INVALID_ARGS;
    }

    truncated_since_last_successful_write_ = true;
    return ZX_OK;
}

} // namespace fs
