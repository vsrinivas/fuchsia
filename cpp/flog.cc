// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>

#include "apps/media/cpp/flog.h"
#include "mojo/public/cpp/application/connect.h"

namespace mojo {
namespace flog {

// static
void Flog::Initialize(Shell* shell, const std::string& label) {
  FTL_DCHECK(!logger_);

  FlogServicePtr flog_service;
  ConnectToService(shell, "mojo:flog_service", GetProxy(&flog_service));
  // TODO(dalesat): Need a thread-safe proxy.

  FlogLoggerPtr flog_logger;
  flog_service->CreateLogger(GetProxy(&flog_logger), label);
  logger_ = flog_logger.Pass();
}

// static
void Flog::LogChannelCreation(uint32_t channel_id,
                              const char* channel_type_name,
                              uint64_t subject_address) {
  if (!logger_) {
    return;
  }

  logger_->LogChannelCreation(GetTime(), channel_id, channel_type_name,
                              subject_address);
}

// static
void Flog::LogChannelMessage(uint32_t channel_id, Message* message) {
  if (!logger_) {
    return;
  }

  Array<uint8_t> array = Array<uint8_t>::New(message->data_num_bytes());
  memcpy(array.data(), message->data(), message->data_num_bytes());
  logger_->LogChannelMessage(GetTime(), channel_id, array.Pass());
}

// static
void Flog::LogChannelDeletion(uint32_t channel_id) {
  if (!logger_) {
    return;
  }

  logger_->LogChannelDeletion(GetTime(), channel_id);
}

// static
uint64_t Flog::GetTime() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// static
std::atomic_ulong Flog::last_allocated_channel_id_;

// static
FlogLoggerPtr Flog::logger_;

FlogChannel::FlogChannel(const char* channel_type_name,
                         uint64_t subject_address)
    : id_(Flog::AllocateChannelId()) {
  Flog::LogChannelCreation(id_, channel_type_name, subject_address);
}

FlogChannel::~FlogChannel() {
  Flog::LogChannelDeletion(id_);
}

bool FlogChannel::Accept(Message* message) {
  Flog::LogChannelMessage(id_, message);
  return true;
}

bool FlogChannel::AcceptWithResponder(Message* message,
                                      MessageReceiver* responder) {
  FTL_DCHECK(false) << "Flog doesn't support messages with responses";
  abort();
}

}  // namespace flog
}  // namespace mojo
