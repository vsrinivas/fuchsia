// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_FLOG_VIEWER_FLOG_VIEWER_H_
#define EXAMPLES_FLOG_VIEWER_FLOG_VIEWER_H_

#include <map>
#include <unordered_set>

#include "examples/flog_viewer/channel.h"
#include "examples/flog_viewer/channel_handler.h"
#include "mojo/services/flog/interfaces/flog.mojom.h"

namespace mojo {

class Shell;

namespace flog {
namespace examples {

// Model class for the flog viewer app.
class FlogViewer {
 public:
  static const std::string kFormatTerse;
  static const std::string kFormatFull;
  static const std::string kFormatDigest;

  FlogViewer();

  ~FlogViewer();

  void AddChannel(uint32_t channel) { channels_.insert(channel); }

  std::string format() { return format_; }

  void set_format(const std::string& format) { format_ = format; }

  // Initializes the viewer.
  void Initialize(Shell* shell,
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

 private:
  void ProcessEntries(uint32_t start_index);

  void ProcessEntry(uint32_t entry_index, const FlogEntryPtr& entry);

  void PrintRemainingAccumulators();

  void OnMojoLoggerMessage(uint32_t entry_index,
                           const FlogEntryPtr& entry,
                           const FlogMojoLoggerMessageEntryDetailsPtr& details);

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
  static const uint32_t kGetEntriesMaxCount = 1024;

  std::unordered_set<uint32_t> channels_;
  std::string format_ = kFormatTerse;
  std::function<void()> terminate_callback_;
  FlogServicePtr service_;
  FlogReaderPtr reader_;
  std::map<uint32_t, std::shared_ptr<Channel>> channels_by_channel_id_;
  std::map<uint64_t, std::shared_ptr<Channel>> channels_by_subject_address_;
};

}  // namespace examples
}  // namespace flog
}  // namespace mojo

#endif  // EXAMPLES_FLOG_VIEWER_FLOG_VIEWER_H_
