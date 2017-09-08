// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/flog_service/flog_reader_impl.h"

#include <unistd.h>

#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/logging.h"
#include "lib/fidl/cpp/bindings/internal/array_serialization.h"
#include "lib/fidl/cpp/bindings/internal/string_serialization.h"

namespace flog {

// static
std::shared_ptr<FlogReaderImpl> FlogReaderImpl::Create(
    fidl::InterfaceRequest<FlogReader> request,
    uint32_t log_id,
    const std::string& label,
    std::shared_ptr<FlogDirectory> directory,
    FlogServiceImpl* owner) {
  return std::shared_ptr<FlogReaderImpl>(
      new FlogReaderImpl(std::move(request), log_id, label, directory, owner));
}

FlogReaderImpl::FlogReaderImpl(fidl::InterfaceRequest<FlogReader> request,
                               uint32_t log_id,
                               const std::string& label,
                               std::shared_ptr<FlogDirectory> directory,
                               FlogServiceImpl* owner)
    : FlogServiceImpl::Product<FlogReader>(this, std::move(request), owner),
      log_id_(log_id),
      fd_(directory->GetFile(log_id, label, false)) {
  FillReadBuffer(true);
  stub_.set_sink(this);
}

FlogReaderImpl::~FlogReaderImpl() {}

void FlogReaderImpl::GetEntries(uint32_t start_index,
                                uint32_t max_count,
                                const GetEntriesCallback& callback) {
  if (fault_) {
    callback(fidl::Array<FlogEntryPtr>::New(0));
    return;
  }

  if (current_entry_index_ > start_index) {
    current_entry_index_ = 0;
    FillReadBuffer(true);
  }

  while (current_entry_index_ < start_index) {
    if (!DiscardEntry()) {
      callback(fidl::Array<FlogEntryPtr>::New(0));
      return;
    }
  }

  FTL_DCHECK(current_entry_index_ == start_index);

  fidl::Array<FlogEntryPtr> entries = fidl::Array<FlogEntryPtr>::New(max_count);

  for (uint32_t i = 0; i < max_count; i++) {
    FlogEntryPtr entry = GetEntry();
    if (!entry) {
      if (fault_) {
        callback(fidl::Array<FlogEntryPtr>::New(0));
        return;
      }

      // Reached end-of-file.
      entries.resize(i);
      callback(std::move(entries));
      return;
    }

    entries[i] = std::move(entry);
  }

  callback(std::move(entries));
}

bool FlogReaderImpl::DiscardEntry() {
  uint32_t message_size;
  size_t bytes_read = ReadData(sizeof(message_size), &message_size);
  if (bytes_read < sizeof(message_size)) {
    if (bytes_read != 0) {
      FTL_DLOG(WARNING) << "FlogReaderImpl::DiscardEntry: FAULT: bytes_read < "
                           "sizeof(message_size)";
    }
    fault_ = bytes_read != 0;
    return false;
  }

  if (message_size == 0) {
    FTL_DLOG(WARNING)
        << "FlogReaderImpl::DiscardEntry: FAULT: message_size == 0";
    fault_ = true;
    return false;
  }

  bytes_read = ReadData(message_size, nullptr);
  if (bytes_read < message_size) {
    FTL_DLOG(WARNING)
        << "FlogReaderImpl::DiscardEntry: FAULT: bytes_read < message_size";
    fault_ = true;
    return false;
  }

  ++current_entry_index_;

  return true;
}

FlogEntryPtr FlogReaderImpl::GetEntry() {
  uint32_t message_size;
  size_t bytes_read = ReadData(sizeof(message_size), &message_size);
  if (bytes_read < sizeof(message_size)) {
    if (bytes_read != 0) {
      FTL_DLOG(WARNING) << "FlogReaderImpl::GetEntry: FAULT: bytes_read < "
                           "sizeof(message_size)";
    }
    fault_ = bytes_read != 0;
    return nullptr;
  }

  if (message_size == 0) {
    FTL_DLOG(WARNING) << "FlogReaderImpl::GetEntry: FAULT: message_size == 0";
    fault_ = true;
    return nullptr;
  }

  std::unique_ptr<fidl::Message> message =
      std::unique_ptr<fidl::Message>(new fidl::Message());
  message->AllocUninitializedData(message_size);

  bytes_read = ReadData(message_size, message->mutable_data());
  if (bytes_read < message_size) {
    FTL_DLOG(WARNING)
        << "FlogReaderImpl::GetEntry: FAULT: bytes_read < message_size";
    fault_ = true;
    return nullptr;
  }

  ++current_entry_index_;

  // Use the stub to deserialize into entry_.
  stub_.Accept(message.get());
  FTL_DCHECK(entry_);
  return std::move(entry_);
}

size_t FlogReaderImpl::ReadData(size_t data_size, void* data) {
  FTL_DCHECK(data_size != 0);

  while (read_buffer_bytes_remaining() == 0) {
    if (read_buffer_.size() < kReadBufferSize) {
      // read_buffer_ is exhausted and short (because we reached end of file).
      return 0;
    }

    FillReadBuffer(false);
  }

  // Copy up to data_size bytes from the buffer.
  uint32_t initial_data_size = read_buffer_bytes_remaining();
  if (initial_data_size > data_size) {
    initial_data_size = data_size;
  }

  if (data != nullptr) {
    memcpy(data, read_buffer_.data() + read_buffer_bytes_used_,
           initial_data_size);
  }

  read_buffer_bytes_used_ += initial_data_size;

  if (initial_data_size == data_size || read_buffer_.size() < kReadBufferSize) {
    // Either read_buffer_ contained all the required data, or read_buffer_ is
    // short, indicating we've hit end-of-file.
    return initial_data_size;
  }

  FTL_DCHECK(read_buffer_bytes_remaining() == 0);

  // Read the remainder.
  return ReadData(data_size - initial_data_size,
                  reinterpret_cast<uint8_t*>(data) + initial_data_size) +
         initial_data_size;
}

void FlogReaderImpl::FillReadBuffer(bool restart) {
  if (restart) {
    lseek(fd_.get(), 0, SEEK_SET);
  }

  read_buffer_.resize(kReadBufferSize);
  read_buffer_bytes_used_ = 0;

  ssize_t bytes_read =
      HANDLE_EINTR(read(fd_.get(), read_buffer_.data(), read_buffer_.size()));
  if (bytes_read < 0) {
    FTL_DLOG(WARNING) << "FlogReaderImpl::FillReadBuffer failed";
    fault_ = true;
    read_buffer_.clear();
    return;
  }

  read_buffer_.resize(bytes_read);
}

FlogEntryPtr FlogReaderImpl::CreateEntry(int64_t time_ns, uint32_t channel_id) {
  FlogEntryPtr entry = FlogEntry::New();
  entry->time_ns = time_ns;
  entry->log_id = log_id_;
  entry->channel_id = channel_id;
  entry->details = FlogEntryDetails::New();
  return entry;
}

void FlogReaderImpl::LogChannelCreation(int64_t time_ns,
                                        uint32_t channel_id,
                                        const fidl::String& type_name,
                                        uint64_t subject_address) {
  entry_ = CreateEntry(time_ns, channel_id);
  FlogChannelCreationEntryDetailsPtr details =
      FlogChannelCreationEntryDetails::New();
  details->type_name = type_name;
  details->subject_address = subject_address;
  entry_->details->set_channel_creation(std::move(details));
}

void FlogReaderImpl::LogChannelMessage(int64_t time_ns,
                                       uint32_t channel_id,
                                       fidl::Array<uint8_t> data) {
  entry_ = CreateEntry(time_ns, channel_id);
  FlogChannelMessageEntryDetailsPtr details =
      FlogChannelMessageEntryDetails::New();
  details->data = std::move(data);
  entry_->details->set_channel_message(std::move(details));
}

void FlogReaderImpl::LogChannelDeletion(int64_t time_ns, uint32_t channel_id) {
  entry_ = CreateEntry(time_ns, channel_id);
  FlogChannelDeletionEntryDetailsPtr details =
      FlogChannelDeletionEntryDetails::New();
  entry_->details->set_channel_deletion(std::move(details));
}

}  // namespace flog
