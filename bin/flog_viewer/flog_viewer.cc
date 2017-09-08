// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/flog_viewer/flog_viewer.h"

#include <iomanip>
#include <iostream>
#include <limits>

#include "application/lib/app/connect.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "garnet/bin/flog_viewer/formatting.h"
#include "lib/fidl/cpp/bindings/message.h"

namespace flog {

FlogViewer::FlogViewer() {}

FlogViewer::~FlogViewer() {}

void FlogViewer::Initialize(app::ApplicationContext* application_context,
                            const std::function<void()>& terminate_callback) {
  terminate_callback_ = terminate_callback;
  service_ = application_context->ConnectToEnvironmentService<FlogService>();
  service_.set_connection_error_handler([this]() {
    FTL_LOG(ERROR) << "FlogService connection failed";
    service_.reset();
    terminate_callback_();
  });
}

void FlogViewer::ProcessLogs() {
  FTL_DCHECK(service_);

  service_->GetLogDescriptions(
      [this](fidl::Array<FlogDescriptionPtr> descriptions) {
        std::cout << "\n";
        std::cout << "     id  label\n";
        std::cout << "-------- ---------------------------------------------"
                  << "\n";

        for (const FlogDescriptionPtr& description : descriptions) {
          std::cout << std::setw(8) << description->log_id << " "
                    << description->label << "\n";
        }

        std::cout << "\n";

        terminate_callback_();
      });
}

void FlogViewer::ProcessLogs(const std::vector<uint32_t>& log_ids) {
  FTL_DCHECK(service_);

  for (uint32_t log_id : log_ids) {
    FTL_DCHECK(log_id != 0);
    service_->CreateReader(logs_by_id_[log_id].reader_.NewRequest(), log_id);
  }

  ProcessEntries();
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
    uint32_t log_id,
    uint64_t subject_address) {
  if (subject_address == 0) {
    return nullptr;
  }

  auto& channels_by_subject_address =
      logs_by_id_[log_id].channels_by_subject_address_;

  auto iter = channels_by_subject_address.find(subject_address);
  if (iter != channels_by_subject_address.end()) {
    return iter->second;
  }

  std::shared_ptr<Channel> channel =
      Channel::CreateUnresolved(log_id, subject_address);
  channels_by_subject_address.insert(std::make_pair(subject_address, channel));
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

void FlogViewer::ProcessEntries() {
  std::shared_ptr<media::CallbackJoiner> callback_joiner =
      media::CallbackJoiner::Create();

  for (auto& pair : logs_by_id_) {
    pair.second.GetEntries(0, callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined([this]() { ProcessLoadedEntries(); });
}

void FlogViewer::ProcessLoadedEntries() {
  while (true) {
    int64_t best_time = std::numeric_limits<int64_t>::max();
    Log* best_log = nullptr;

    for (auto& pair : logs_by_id_) {
      Log& log = pair.second;

      if (log.exhausted()) {
        continue;
      }

      FTL_DCHECK(!log.consumed());

      if (best_time > log.current_entry()->time_ns) {
        best_time = log.current_entry()->time_ns;
        best_log = &log;
      }
    }

    if (best_log == nullptr) {
      PrintRemainingAccumulators();
      terminate_callback_();
      break;
    }

    ProcessEntry(best_log->current_entry_index(), best_log->current_entry());

    if (stop_index_.first == best_log->current_entry()->log_id &&
        stop_index_.second == best_log->current_entry_index()) {
      PrintRemainingAccumulators();
      terminate_callback_();
      break;
    }

    best_log->ConsumeEntry();

    if (best_log->consumed() && !best_log->exhausted()) {
      best_log->GetEntries(best_log->current_entry_index(),
                           [this]() { ProcessLoadedEntries(); });
      break;
    }
  }
}

void FlogViewer::ProcessEntry(uint32_t entry_index, const FlogEntryPtr& entry) {
  Log& log = logs_by_id_[entry->log_id];

  if (!log.enabled_channels_.empty() &&
      log.enabled_channels_.count(entry->channel_id) == 0) {
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
    std::cout << EntryHeader(entry, entry_index) << "NO KNOWN DETAILS\n";
  }
}

void FlogViewer::PrintRemainingAccumulators() {
  if (format_ != ChannelHandler::kFormatDigest) {
    return;
  }

  for (auto& log_pair : logs_by_id_) {
    Log& log = log_pair.second;

    for (std::pair<uint32_t, std::shared_ptr<Channel>> pair :
         log.channels_by_channel_id_) {
      if (pair.second->has_accumulator() && !pair.second->has_parent()) {
        std::cout << "\n" << *pair.second << " ";
        pair.second->PrintAccumulator(std::cout);
        std::cout << "\n";
      }
    }
  }
}

void FlogViewer::OnChannelCreated(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    const FlogChannelCreationEntryDetailsPtr& details) {
  if (format_ == ChannelHandler::kFormatTerse ||
      format_ == ChannelHandler::kFormatFull) {
    std::cout << EntryHeader(entry, entry_index) << "channel created, type "
              << details->type_name << ", address "
              << AsAddress(details->subject_address) << "\n";
  }

  auto& channels_by_channel_id =
      logs_by_id_[entry->log_id].channels_by_channel_id_;

  auto iter = channels_by_channel_id.find(entry->channel_id);
  if (iter != channels_by_channel_id.end()) {
    std::cout << EntryHeader(entry, entry_index)
              << "ERROR: CHANNEL ALREADY EXISTS\n";
  }

  std::shared_ptr<Channel> channel;

  auto& channels_by_subject_address =
      logs_by_id_[entry->log_id].channels_by_subject_address_;

  auto subject_iter =
      channels_by_subject_address.find(details->subject_address);
  if (subject_iter != channels_by_subject_address.end()) {
    if (subject_iter->second->resolved()) {
      std::cout << EntryHeader(entry, entry_index)
                << "ERROR: NEW CHANNEL SHARES SUBJECT ADDRESS WITH "
                   "EXISTING CHANNEL "
                << *subject_iter->second << "\n";
    } else {
      channel = subject_iter->second;
      channel->Resolve(
          entry->channel_id, entry_index,
          ChannelHandler::Create(details->type_name, format_, this));
    }
  }

  if (!channel) {
    channel = Channel::Create(
        entry->log_id, entry->channel_id, entry_index, details->subject_address,
        ChannelHandler::Create(details->type_name, format_, this));
    if (details->subject_address != 0) {
      channels_by_subject_address.insert(
          std::make_pair(details->subject_address, channel));
    }
  }

  channels_by_channel_id.insert(std::make_pair(entry->channel_id, channel));
}

void FlogViewer::OnChannelMessage(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    const FlogChannelMessageEntryDetailsPtr& details) {
  fidl::Message message;
  message.AllocUninitializedData(details->data.size());
  memcpy(message.mutable_data(), details->data.data(), details->data.size());

  auto& channels_by_channel_id =
      logs_by_id_[entry->log_id].channels_by_channel_id_;

  auto iter = channels_by_channel_id.find(entry->channel_id);
  if (iter == channels_by_channel_id.end()) {
    std::cout << EntryHeader(entry, entry_index)
              << "ERROR: CHANNEL DOESN'T EXIST\n";
    return;
  }

  iter->second->handler()->HandleMessage(iter->second, entry_index, entry,
                                         &message);
}

void FlogViewer::OnChannelDeleted(
    uint32_t entry_index,
    const FlogEntryPtr& entry,
    const FlogChannelDeletionEntryDetailsPtr& details) {
  if (format_ == ChannelHandler::kFormatTerse ||
      format_ == ChannelHandler::kFormatFull) {
    std::cout << EntryHeader(entry, entry_index) << "channel deleted\n";
  }

  auto& channels_by_channel_id =
      logs_by_id_[entry->log_id].channels_by_channel_id_;

  auto iter = channels_by_channel_id.find(entry->channel_id);
  if (iter == channels_by_channel_id.end()) {
    std::cout << EntryHeader(entry, entry_index)
              << "ERROR: CHANNEL DOESN'T EXIST\n";
    return;
  }

  if (format_ == ChannelHandler::kFormatDigest &&
      iter->second->has_accumulator()) {
    std::cout << "\nDELETED " << *iter->second << " ";
    iter->second->PrintAccumulator(std::cout);
  }

  auto& channels_by_subject_address =
      logs_by_id_[entry->log_id].channels_by_subject_address_;

  channels_by_subject_address.erase(iter->second->subject_address());

  channels_by_channel_id.erase(iter);
}

void FlogViewer::Log::GetEntries(uint32_t start_index,
                                 const std::function<void()>& callback) {
  first_entry_index_ = start_index;
  entries_consumed_ = 0;
  reader_->GetEntries(
      start_index, kGetEntriesMaxCount,
      [this, start_index, callback](fidl::Array<FlogEntryPtr> entries) {
        entries_ = std::move(entries);
        callback();
      });
}

}  // namespace flog
