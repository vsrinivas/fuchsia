// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iomanip>
#include <iostream>

#include "base/logging.h"
#include "services/flog/flog_logger_impl.h"

namespace mojo {
namespace flog {

namespace {

struct AsMicroseconds {
  explicit AsMicroseconds(uint64_t time_us) : time_us_(time_us) {}
  uint64_t time_us_;
};

std::ostream& operator<<(std::ostream& os, AsMicroseconds value) {
  return os << std::setfill('0') << std::setw(6) << value.time_us_ % 1000000ull;
}

struct AsLogLevel {
  explicit AsLogLevel(uint32_t level) : level_(level) {}
  uint32_t level_;
};

std::ostream& operator<<(std::ostream& os, AsLogLevel value) {
  switch (value.level_) {
    case MOJO_LOG_LEVEL_VERBOSE:
      return os << "VERBOSE";
    case MOJO_LOG_LEVEL_INFO:
      return os << "INFO";
    case MOJO_LOG_LEVEL_WARNING:
      return os << "WARNING";
    case MOJO_LOG_LEVEL_ERROR:
      return os << "ERROR";
    case MOJO_LOG_LEVEL_FATAL:
      return os << "FATAL";
    default:
      return os << "UNKNOWN LEVEL " << value.level_;
  }
}

}  // namespace

// static
std::shared_ptr<FlogLoggerImpl> FlogLoggerImpl::Create(
    InterfaceRequest<FlogLogger> request,
    uint32_t log_id,
    const std::string& label,
    std::shared_ptr<FlogDirectory> directory,
    FlogServiceImpl* owner) {
  return std::shared_ptr<FlogLoggerImpl>(
      new FlogLoggerImpl(request.Pass(), log_id, label, directory, owner));
}

FlogLoggerImpl::FlogLoggerImpl(InterfaceRequest<FlogLogger> request,
                               uint32_t log_id,
                               const std::string& label,
                               std::shared_ptr<FlogDirectory> directory,
                               FlogServiceImpl* owner)
    : FlogServiceImpl::ProductBase(owner),
      file_(directory->GetFile(log_id, label, true)) {
  mojo::internal::MessageValidatorList validators;
  router_.reset(new mojo::internal::Router(
      request.PassMessagePipe(), std::move(validators),
      Environment::GetDefaultAsyncWaiter()));
  router_->set_incoming_receiver(this);
  router_->set_connection_error_handler([this]() { ReleaseFromOwner(); });
  stub_.set_sink(this);
}

FlogLoggerImpl::~FlogLoggerImpl() {}

bool FlogLoggerImpl::Accept(Message* message) {
  DCHECK(message != nullptr);
  DCHECK(message->data_num_bytes() > 0);
  DCHECK(message->data() != nullptr);

    uint32_t message_size = message->data_num_bytes();

  WriteData(sizeof(message_size), &message_size);
  WriteData(message_size, message->data());

  // Call the stub to output mojo logger messages to stderr. This has to
  // happen after we write the message to the file, because deserialization
  // mutates the message. We only do this for LogMojoLoggerMessage.
  // TODO(dalesat): Provide a switch to turn this off.
  if (static_cast<mojo::flog::internal::FlogLogger_Base::MessageOrdinals>(
          message->header()->name) ==
      mojo::flog::internal::FlogLogger_Base::MessageOrdinals::
          LogMojoLoggerMessage) {
    stub_.Accept(message);
  }

  return true;
}

bool FlogLoggerImpl::AcceptWithResponder(Message* message,
                                         MessageReceiverWithStatus* responder) {
  DCHECK(false) << "FlogLogger has no methods with responses";
  abort();
}

void FlogLoggerImpl::WriteData(uint32_t data_size, const void* data) {
  DCHECK(data_size > 0);
  DCHECK(data != nullptr);
  DCHECK(file_);

  Array<uint8_t> bytes_to_write = Array<uint8_t>::New(data_size);
  memcpy(bytes_to_write.data(), data, data_size);
  file_->Write(bytes_to_write.Pass(), 0, files::Whence::FROM_CURRENT,
               [data_size](files::Error error, uint32 bytes_written) {
                 DCHECK(error == files::Error::OK);
                 DCHECK(bytes_written == data_size);
                 // TODO(dalesat): Handle error.
               });
}

void FlogLoggerImpl::LogMojoLoggerMessage(int64_t time_us,
                                          int32_t log_level,
                                          const String& message,
                                          const String& source_file,
                                          uint32_t source_line) {
  std::cerr << AsMicroseconds(time_us) << " " << AsLogLevel(log_level) << ":"
            << source_file << "#" << source_line << " " << message << std::endl;
}

void FlogLoggerImpl::LogChannelCreation(int64_t time_us,
                                        uint32_t channel_id,
                                        const String& type_name,
                                        uint64_t subject_address) {}

void FlogLoggerImpl::LogChannelMessage(int64_t time_us,
                                       uint32_t channel_id,
                                       mojo::Array<uint8_t> data) {}

void FlogLoggerImpl::LogChannelDeletion(int64_t time_us, uint32_t channel_id) {}

}  // namespace flog
}  // namespace mojo
