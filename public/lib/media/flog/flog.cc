// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/media/flog/flog.h"

#include <trace/event.h>

#include "lib/fxl/time/time_delta.h"
#include "lib/fxl/time/time_point.h"

namespace flog {

#ifdef FLOG_ENABLED

// static
uint64_t Flog::next_entry_index_ = 0;

// static
void Flog::Initialize(app::ApplicationContext* application_context,
                      const std::string& label) {
  FXL_DCHECK(!logger_);

  FlogServicePtr flog_service =
      application_context->ConnectToEnvironmentService<FlogService>();
  // TODO(dalesat): Need a thread-safe proxy.

  FlogLoggerPtr flog_logger;
  flog_service->CreateLogger(flog_logger.NewRequest(), label);
  logger_ = std::move(flog_logger);
}

// static
void Flog::LogChannelCreation(uint32_t channel_id,
                              const char* channel_type_name,
                              uint64_t subject_address) {
  if (!logger_) {
    return;
  }

  TRACE_INSTANT("motown", "flog create channel", TRACE_SCOPE_PROCESS, "index",
                next_entry_index_, "channel_id", channel_id);

  logger_->LogChannelCreation(GetTime(), channel_id, channel_type_name,
                              subject_address);
  ++next_entry_index_;
}

// static
void Flog::LogChannelMessage(uint32_t channel_id, fidl::Message* message) {
  if (!logger_) {
    return;
  }

  TRACE_INSTANT("motown", "flog message", TRACE_SCOPE_PROCESS, "index",
                next_entry_index_, "channel_id", channel_id);

  fidl::Array<uint8_t> array =
      fidl::Array<uint8_t>::New(message->data_num_bytes());
  memcpy(array.data(), message->data(), message->data_num_bytes());
  logger_->LogChannelMessage(GetTime(), channel_id, std::move(array));
  ++next_entry_index_;
}

// static
void Flog::LogChannelDeletion(uint32_t channel_id) {
  if (!logger_) {
    return;
  }

  TRACE_INSTANT("motown", "flog delete channel", TRACE_SCOPE_PROCESS, "index",
                next_entry_index_, "channel_id", channel_id);

  logger_->LogChannelDeletion(GetTime(), channel_id);
  ++next_entry_index_;
}

// static
int64_t Flog::GetTime() {
  return (fxl::TimePoint::Now() - fxl::TimePoint()).ToNanoseconds();
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

bool FlogChannel::Accept(fidl::Message* message) {
  Flog::LogChannelMessage(id_, message);
  return true;
}

bool FlogChannel::AcceptWithResponder(fidl::Message* message,
                                      MessageReceiver* responder) {
  FXL_DCHECK(false) << "Flog doesn't support messages with responses";
  abort();
}

#endif  // FLOG_ENABLED

}  // namespace flog
