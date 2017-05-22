// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <unordered_set>

#include "application/lib/app/application_context.h"
#include "apps/media/services/flog/flog.fidl.h"
#include "apps/media/tools/flog_viewer/channel.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"
#include "apps/media/tools/flog_viewer/channel_manager.h"

class Shell;

namespace flog {

// Model class for the flog viewer app.
class FlogViewer : public ChannelManager {
 public:
  FlogViewer();

  ~FlogViewer();

  void AddChannel(uint32_t channel) { channels_.insert(channel); }

  std::string format() { return format_; }

  void set_format(const std::string& format) { format_ = format; }

  void set_stop_index(uint32_t stop_index) { stop_index_ = stop_index; }

  // Initializes the viewer.
  void Initialize(app::ApplicationContext* application_context,
                  const std::function<void()>& terminate_callback);

  // Processs log descriptions.
  void ProcessLogs();

  // Processs entries from a log.
  void ProcessLog(uint32_t log_id);

  // Process the log with the highest id.
  void ProcessLastLog(const std::string& label);

  // Deletes the specified log file if it isn't currently open.
  void DeleteLog(uint32_t log_id);

  // Deletes all the existing logs files that aren't currently open.
  void DeleteAllLogs();

  // ChannelManager implementation.
  std::shared_ptr<Channel> FindChannelBySubjectAddress(
      uint64_t subject_address) override;

  void SetBindingKoid(Binding* binding, uint64_t koid) override;

  void BindAs(std::shared_ptr<Channel> channel, uint64_t koid) override;

 private:
  void ProcessEntries(uint32_t start_index);

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

  std::unordered_set<uint32_t> channels_;
  std::string format_ = ChannelHandler::kFormatDigest;
  std::function<void()> terminate_callback_;
  FlogServicePtr service_;
  FlogReaderPtr reader_;
  std::map<uint32_t, std::shared_ptr<Channel>> channels_by_channel_id_;
  std::map<uint64_t, std::shared_ptr<Channel>> channels_by_subject_address_;
  std::map<uint64_t, std::shared_ptr<Channel>> channels_by_binding_koid_;
  std::map<uint64_t, Binding*> bindings_by_binding_koid_;
  uint32_t stop_index_;
};

}  // namespace flog
