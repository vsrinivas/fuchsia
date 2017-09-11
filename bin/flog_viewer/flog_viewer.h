// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <unordered_set>

#include "lib/app/cpp/application_context.h"
#include "lib/media/fidl/flog/flog.fidl.h"
#include "garnet/bin/flog_viewer/channel.h"
#include "garnet/bin/flog_viewer/channel_handler.h"
#include "garnet/bin/flog_viewer/channel_manager.h"

class Shell;

namespace flog {

// Model class for the flog viewer app.
class FlogViewer : public ChannelManager {
 public:
  FlogViewer();

  ~FlogViewer();

  void EnableChannel(const std::pair<uint32_t, uint32_t> channel) {
    logs_by_id_[channel.first].enabled_channels_.insert(channel.second);
  }

  std::string format() { return format_; }

  void set_format(const std::string& format) { format_ = format; }

  void set_stop_index(const std::pair<uint32_t, uint32_t>& stop_index) {
    stop_index_ = stop_index;
  }

  // Initializes the viewer.
  void Initialize(app::ApplicationContext* application_context,
                  const std::function<void()>& terminate_callback);

  // Processs log descriptions.
  void ProcessLogs();

  // Processs entries from a set of logs.
  void ProcessLogs(const std::vector<uint32_t>& log_id);

  // Deletes the specified log file if it isn't currently open.
  void DeleteLog(uint32_t log_id);

  // Deletes all the existing logs files that aren't currently open.
  void DeleteAllLogs();

  // ChannelManager implementation.
  std::shared_ptr<Channel> FindChannelBySubjectAddress(
      uint32_t log_id,
      uint64_t subject_address) override;

  void SetBindingKoid(Binding* binding, uint64_t koid) override;

  void BindAs(std::shared_ptr<Channel> channel, uint64_t koid) override;

 private:
  void ProcessEntries();

  void ProcessLoadedEntries();

  void ProcessEntry(uint32_t entry_index, const FlogEntryPtr& entry);

  void PrintRemainingAccumulators();

  void OnChannelCreated(uint32_t entry_index,
                        const FlogEntryPtr& entry,
                        const FlogChannelCreationEntryDetailsPtr& details);

  void OnChannelMessage(uint32_t entry_index,
                        const FlogEntryPtr& entry,
                        const FlogChannelMessageEntryDetailsPtr& details);

  void OnChannelDeleted(uint32_t entry_index,
                        const FlogEntryPtr& entry,
                        const FlogChannelDeletionEntryDetailsPtr& details);

 private:
  // TODO(dalesat): This was reduced from 1024 as a workaround. Change back.
  static const uint32_t kGetEntriesMaxCount = 64;

  struct Log {
    FlogReaderPtr reader_;
    fidl::Array<FlogEntryPtr> entries_;
    uint32_t first_entry_index_;
    uint32_t entries_consumed_;
    std::unordered_set<uint32_t> enabled_channels_;
    std::map<uint32_t, std::shared_ptr<Channel>> channels_by_channel_id_;
    std::map<uint64_t, std::shared_ptr<Channel>> channels_by_subject_address_;

    uint32_t current_entry_index() const {
      return first_entry_index_ + entries_consumed_;
    }

    const FlogEntryPtr& current_entry() const {
      return entries_[entries_consumed_];
    }

    void ConsumeEntry() {
      FXL_DCHECK(!consumed());
      ++entries_consumed_;
    }

    bool consumed() const { return entries_consumed_ == entries_.size(); }

    bool exhausted() const {
      return consumed() && entries_.size() < kGetEntriesMaxCount;
    }

    void GetEntries(uint32_t start_index,
                    const std::function<void()>& callback);
  };

  std::string format_ = ChannelHandler::kFormatDigest;
  std::function<void()> terminate_callback_;
  FlogServicePtr service_;
  std::map<uint32_t, Log> logs_by_id_;
  std::map<uint64_t, std::shared_ptr<Channel>> channels_by_binding_koid_;
  std::map<uint64_t, Binding*> bindings_by_binding_koid_;
  std::pair<uint32_t, uint32_t> stop_index_ =
      std::pair<uint32_t, uint32_t>(0, 0);
};

}  // namespace flog
