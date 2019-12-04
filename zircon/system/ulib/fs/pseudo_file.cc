// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo_file.h>

#include <fuchsia/io/llcpp/fidl.h>
#include <zircon/device/vfs.h>

#include <utility>

#include <fs/vfs.h>
#include <fs/vfs_types.h>

namespace fio = ::llcpp::fuchsia::io;

namespace fs {

PseudoFile::PseudoFile(ReadHandler read_handler, WriteHandler write_handler)
    : read_handler_(std::move(read_handler)), write_handler_(std::move(write_handler)) {}

PseudoFile::~PseudoFile() = default;

VnodeProtocolSet PseudoFile::GetProtocols() const { return VnodeProtocol::kFile; }

bool PseudoFile::ValidateRights(Rights rights) {
  if (rights.read && !read_handler_) {
    return false;
  }
  if (rights.write && !write_handler_) {
    return false;
  }
  return true;
}

zx_status_t PseudoFile::GetAttributes(VnodeAttributes* attr) {
  *attr = VnodeAttributes();
  attr->mode = V_TYPE_FILE;
  if (read_handler_)
    attr->mode |= V_IRUSR;
  if (write_handler_)
    attr->mode |= V_IWUSR;
  attr->inode = fio::INO_UNKNOWN;
  attr->link_count = 1;
  return ZX_OK;
}

zx_status_t PseudoFile::GetNodeInfoForProtocol([[maybe_unused]] VnodeProtocol protocol,
                                               [[maybe_unused]] Rights rights,
                                               VnodeRepresentation* info) {
  *info = VnodeRepresentation::File();
  return ZX_OK;
}

BufferedPseudoFile::BufferedPseudoFile(ReadHandler read_handler, WriteHandler write_handler,
                                       size_t input_buffer_capacity)
    : PseudoFile(std::move(read_handler), std::move(write_handler)),
      input_buffer_capacity_(input_buffer_capacity) {}

BufferedPseudoFile::~BufferedPseudoFile() = default;

zx_status_t BufferedPseudoFile::Open(ValidatedOptions options,
                                     fbl::RefPtr<Vnode>* out_redirect) {
  fbl::String output;
  if (options->rights.read) {
    zx_status_t status = read_handler_(&output);
    if (status != ZX_OK) {
      return status;
    }
  }

  *out_redirect = fbl::AdoptRef(new Content(fbl::RefPtr(this), *options, std::move(output)));
  return ZX_OK;
}

BufferedPseudoFile::Content::Content(fbl::RefPtr<BufferedPseudoFile> file,
                                     VnodeConnectionOptions options, fbl::String output)
    : file_(std::move(file)), options_(options), output_(std::move(output)) {}

BufferedPseudoFile::Content::~Content() { delete[] input_data_; }

VnodeProtocolSet BufferedPseudoFile::Content::GetProtocols() const { return VnodeProtocol::kFile; }

zx_status_t BufferedPseudoFile::Content::Close() {
  if (options_.rights.write) {
    return file_->write_handler_(fbl::StringPiece(input_data_, input_length_));
  }
  return ZX_OK;
}

zx_status_t BufferedPseudoFile::Content::GetAttributes(fs::VnodeAttributes* a) {
  return file_->GetAttributes(a);
}

zx_status_t BufferedPseudoFile::Content::Read(void* data, size_t length, size_t offset,
                                              size_t* out_actual) {
  ZX_DEBUG_ASSERT(options_.rights.read);

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
  ZX_DEBUG_ASSERT(options_.rights.write);

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
  ZX_DEBUG_ASSERT(options_.rights.write);

  zx_status_t status = Write(data, length, input_length_, out_actual);
  if (status == ZX_OK) {
    *out_end = input_length_;
  }
  return status;
}

zx_status_t BufferedPseudoFile::Content::Truncate(size_t length) {
  ZX_DEBUG_ASSERT(options_.rights.write);

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

zx_status_t BufferedPseudoFile::Content::GetNodeInfoForProtocol(VnodeProtocol protocol,
                                                                Rights rights,
                                                                VnodeRepresentation* info) {
  return file_->GetNodeInfoForProtocol(protocol, rights, info);
}

void BufferedPseudoFile::Content::SetInputLength(size_t length) {
  ZX_DEBUG_ASSERT(length <= file_->input_buffer_capacity_);

  if (input_data_ == nullptr && length != 0u) {
    input_data_ = new char[file_->input_buffer_capacity_];
  }
  input_length_ = length;
}

UnbufferedPseudoFile::UnbufferedPseudoFile(ReadHandler read_handler, WriteHandler write_handler)
    : PseudoFile(std::move(read_handler), std::move(write_handler)) {}

UnbufferedPseudoFile::~UnbufferedPseudoFile() = default;

VnodeProtocolSet UnbufferedPseudoFile::Content::GetProtocols() const { return VnodeProtocol::kFile; }

zx_status_t UnbufferedPseudoFile::Open(ValidatedOptions options,
                                       fbl::RefPtr<Vnode>* out_redirect) {
  *out_redirect = fbl::AdoptRef(new Content(fbl::RefPtr(this), *options));
  return ZX_OK;
}

UnbufferedPseudoFile::Content::Content(fbl::RefPtr<UnbufferedPseudoFile> file,
                                       VnodeConnectionOptions options)
    : file_(std::move(file)),
      options_(options),
      truncated_since_last_successful_write_(options.flags.create || options.flags.truncate) {}

UnbufferedPseudoFile::Content::~Content() = default;

zx_status_t UnbufferedPseudoFile::Content::Open(ValidatedOptions options,
                                                fbl::RefPtr<Vnode>* out_redirect) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UnbufferedPseudoFile::Content::Close() {
  if (options_.rights.write && truncated_since_last_successful_write_) {
    return file_->write_handler_(fbl::StringPiece());
  }
  return ZX_OK;
}

zx_status_t UnbufferedPseudoFile::Content::GetAttributes(fs::VnodeAttributes* a) {
  return file_->GetAttributes(a);
}

zx_status_t UnbufferedPseudoFile::Content::Read(void* data, size_t length, size_t offset,
                                                size_t* out_actual) {
  ZX_DEBUG_ASSERT(options_.rights.read);

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
  ZX_DEBUG_ASSERT(options_.rights.write);

  if (offset != 0u) {
    // If the offset is non-zero, we assume the client already wrote the property.
    // Simulate an inability to write additional data.
    return ZX_ERR_NO_SPACE;
  }

  zx_status_t status =
      file_->write_handler_(fbl::StringPiece(static_cast<const char*>(data), length));
  if (status == ZX_OK) {
    truncated_since_last_successful_write_ = false;
    *out_actual = length;
  }
  return status;
}

zx_status_t UnbufferedPseudoFile::Content::Append(const void* data, size_t length, size_t* out_end,
                                                  size_t* out_actual) {
  ZX_DEBUG_ASSERT(options_.rights.write);

  zx_status_t status = Write(data, length, 0u, out_actual);
  if (status == ZX_OK) {
    *out_end = length;
  }
  return status;
}

zx_status_t UnbufferedPseudoFile::Content::Truncate(size_t length) {
  ZX_DEBUG_ASSERT(options_.rights.write);

  if (length != 0u) {
    return ZX_ERR_INVALID_ARGS;
  }

  truncated_since_last_successful_write_ = true;
  return ZX_OK;
}

zx_status_t UnbufferedPseudoFile::Content::GetNodeInfoForProtocol(
    VnodeProtocol protocol, Rights rights, VnodeRepresentation* info) {
  return file_->GetNodeInfoForProtocol(protocol, rights, info);
}

}  // namespace fs
