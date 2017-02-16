// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/flog_viewer.h"

#include <iomanip>
#include <iostream>

#include "application/lib/app/connect.h"
#include "apps/media/tools/flog_viewer/formatting.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace flog {

// static
const std::string FlogViewer::kFormatTerse = "terse";
// static
const std::string FlogViewer::kFormatFull = "full";
// static
const std::string FlogViewer::kFormatDigest = "digest";

FlogViewer::FlogViewer() {}

FlogViewer::~FlogViewer() {}

void FlogViewer::Initialize(modular::ApplicationContext* application_context,
                            const std::function<void()>& terminate_callback) {
  terminate_callback_ = terminate_callback;
  service_ = application_context->ConnectToEnvironmentService<FlogService>();
  service_.set_connection_error_handler([this]() {
    FTL_LOG(ERROR) << "FlogService connection failed";
    terminate_callback_();
  });
}

void FlogViewer::ProcessLogs() {
  FTL_DCHECK(service_);

  service_->GetLogDescriptions(
      [this](fidl::Array<FlogDescriptionPtr> descriptions) {
        std::cout << std::endl;
        std::cout << "     id  label" << std::endl;
        std::cout << "-------- ---------------------------------------------"
                  << std::endl;

        for (const FlogDescriptionPtr& description : descriptions) {
          std::cout << std::setw(8) << description->log_id << " "
                    << description->label << std::endl;
        }

        std::cout << std::endl;

        terminate_callback_();
      });
}

void FlogViewer::ProcessLog(uint32_t log_id) {
  FTL_DCHECK(log_id != 0);
  FTL_DCHECK(service_);

  service_->CreateReader(reader_.NewRequest(), log_id);

  std::cout << std::endl;
  ProcessEntries(0);
}

void FlogViewer::ProcessLastLog(const std::string& label) {
  FTL_DCHECK(service_);

  service_->GetLogDescriptions(
      [this, label](fidl::Array<FlogDescriptionPtr> descriptions) {
        uint32_t last_id = 0;

        for (const FlogDescriptionPtr& description : descriptions) {
          if ((label.empty() || description->label == label) &&
              last_id < description->log_id) {
            last_id = description->log_id;
          }
        }

        if (last_id == 0) {
          std::cout << std::endl;
          std::cout << "no logs found" << std::endl;
          std::cout << std::endl;
          terminate_callback_();
          return;
        }

        ProcessLog(last_id);
      });
}

void FlogViewer::DeleteLog(uint32_t log_id) {
  FTL_DCHECK(service_);
  service_->DeleteLog(log_id);
}

void FlogViewer::DeleteAllLogs() {
  FTL_DCHECK(service_);
  service_->DeleteAllLogs();
}

std::shared_ptr<Channel> FlogViewer::FindChannelBySubjectAddress(
    uint64_t subject_address) {
  if (subject_address == 0) {
    return nullptr;
  }

  auto iter = channels_by_subject_address_.find(subject_address);
  if (iter != channels_by_subject_address_.end()) {
    return iter->second;
  }

  std::shared_ptr<Channel> channel = Channel::CreateUnresolved(subject_address);
  channels_by_subject_address_.insert(std::make_pair(subject_address, channel));
  return channel;
}

void FlogViewer::SetBindingKoid(Binding* binding, uint64_t koid) {
  binding->SetKoid(koid);

  auto iter = channels_by_binding_koid_.find(koid);
  if (iter != channels_by_binding_koid_.end()) {
    binding->SetChannel(iter->second);
  } else {
    bindings_by_binding_koid_.insert(std::make_pair(koid, binding));
  }
}

void FlogViewer::BindAs(std::shared_ptr<Channel> channel, uint64_t koid) {
  channels_by_binding_koid_.insert(std::make_pair(koid, channel));
  auto iter = bindings_by_binding_koid_.find(koid);
  if (iter != bindings_by_binding_koid_.end()) {
    iter->second->SetChannel(channel);
  }
}

void FlogViewer::ProcessEntries(uint32_t start_index) {
  FTL_DCHECK(reader_);
  reader_->GetEntries(start_index, kGetEntriesMaxCount,
                      [this, start_index](fidl::Array<FlogEntryPtr> entries) {
                        uint32_t entry_index = start_index;
                        for (const FlogEntryPtr& entry : entries) {
                          ProcessEntry(entry_index, entry);
                          entry_index++;
                        }

                        if (entries.size() == kGetEntriesMaxCount) {
                          ProcessEntries(start_index + kGetEntriesMaxCount);
                        } else {
                          std::cout << std::endl;
                          PrintRemainingAccumulators();
                          terminate_callback_();
                        }
                      });
}

void FlogViewer::ProcessEntry(uint32_t entry_index, const FlogEntryPtr& entry) {
  if (!channels_.empty() && channels_.count(entry->channel_id) == 0) {
    return;
  }

  if (entry->details->is_channel_creation()) {
    OnChannelCreated(entry_index, entry,
                     entry->details->get_channel_creation());
  } else if (entry->details->is_channel_message()) {
    OnChannelMessage(entry_index, entry, entry->details->get_channel_message());
  } else if (entry->details->is_channel_deletion()) {
    OnChannelDeleted(entry_index, entry,
                     entry->details->get_channel_deletion());
  } else {
    std::cout << entry << "NO KNOWN DETAILS" << std::endl;
  }
}

void FlogViewer::PrintRemainingAccumulators() {
  for (std::pair<uint32_t, std::shared_ptr<Channel>> pair :
       channels_by_channel_id_) {
    if (pair.second->has_accumulator() && !pair.second->has_parent()) {
      std::cout << *pair.second << " ";
      pair.second->PrintAccumulator(std::cout);
      std::cout << std::endl;
    }
  }
}

void FlogViewer::OnChannelCreated(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    const FlogChannelCreationEntryDetailsPtr& details) {
  if (format_ == kFormatTerse || format_ == kFormatFull) {
    std::cout << entry << "channel created, type " << details->type_name
              << ", address " << AsAddress(details->subject_address)
              << std::endl;
  }

  auto iter = channels_by_channel_id_.find(entry->channel_id);
  if (iter != channels_by_channel_id_.end()) {
    std::cout << entry << "    ERROR: CHANNEL ALREADY EXISTS" << std::endl;
  }

  std::shared_ptr<Channel> channel;

  auto subject_iter =
      channels_by_subject_address_.find(details->subject_address);
  if (subject_iter != channels_by_subject_address_.end()) {
    if (subject_iter->second->resolved()) {
      std::cout << entry
                << "    ERROR: NEW CHANNEL SHARES SUBJECT ADDRESS WITH "
                   "EXISTING CHANNEL "
                << subject_iter->second << std::endl;
    } else {
      channel = subject_iter->second;
      channel->Resolve(
          entry->log_id, entry->channel_id, entry_index,
          ChannelHandler::Create(details->type_name, format_, this));
    }
  }

  if (!channel) {
    channel = Channel::Create(
        entry->log_id, entry->channel_id, entry_index, details->subject_address,
        ChannelHandler::Create(details->type_name, format_, this));
    if (details->subject_address != 0) {
      channels_by_subject_address_.insert(
          std::make_pair(details->subject_address, channel));
    }
  }

  channels_by_channel_id_.insert(std::make_pair(entry->channel_id, channel));
}

void FlogViewer::OnChannelMessage(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    const FlogChannelMessageEntryDetailsPtr& details) {
  fidl::Message message;
  message.AllocUninitializedData(details->data.size());
  memcpy(message.mutable_data(), details->data.data(), details->data.size());

  auto iter = channels_by_channel_id_.find(entry->channel_id);
  if (iter == channels_by_channel_id_.end()) {
    std::cout << entry << "    ERROR: CHANNEL DOESN'T EXIST" << std::endl;
    return;
  }

  iter->second->handler()->HandleMessage(iter->second, entry_index, entry,
                                         &message);
}

void FlogViewer::OnChannelDeleted(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    const FlogChannelDeletionEntryDetailsPtr& details) {
  if (format_ == kFormatTerse || format_ == kFormatFull) {
    std::cout << entry << "channel deleted" << std::endl;
  }

  auto iter = channels_by_channel_id_.find(entry->channel_id);
  if (iter == channels_by_channel_id_.end()) {
    std::cout << entry << "    ERROR: CHANNEL DOESN'T EXIST" << std::endl;
    return;
  }

  if (iter->second->has_accumulator()) {
    iter->second->PrintAccumulator(std::cout);
  }
  channels_by_channel_id_.erase(iter);
}

}  // namespace flog
