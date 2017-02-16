// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/media/services/flog/flog.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/binding.h"
#include "apps/media/tools/flog_viewer/channel_manager.h"
#include "lib/fidl/cpp/bindings/message.h"
#include "lib/ftl/logging.h"

namespace flog {

class Channel;

// Handler for channel messages.
//
// A channel handler is created for each channel that appears in a viewed log.
// ChannelHandler::CreateHandler creates the right channel handler for a given
// type and format. If there's no specific handler for the type/format, the
// default handler is used.
//
// Some channel handlers (particularly the ones for the 'digest' format) will
// produce an 'accumulator', which reflects the handler's understanding of the
// messages that have been handled. The GetAccumulator method can be overridden
// to provide callers access to the accumulator.
class ChannelHandler {
 public:
  static std::unique_ptr<ChannelHandler> Create(const std::string& type_name,
                                                const std::string& format,
                                                ChannelManager* manager);

  virtual ~ChannelHandler();

  // Handles a channel message.
  void HandleMessage(std::shared_ptr<Channel> channel,
                     uint32_t entry_index,
                     const FlogEntryPtr& entry,
                     fidl::Message* message);

  // Gets the accumulator from the handler, if there is one. The default
  // implementation returns a null pointer.
  virtual std::shared_ptr<Accumulator> GetAccumulator();

 protected:
  ChannelHandler();

  virtual void HandleMessage(fidl::Message* message) = 0;

  std::ostream& ReportProblem() {
    FTL_DCHECK(entry_) << "ReportProblem called outside of HandleMessage";
    return GetAccumulator()->ReportProblem(entry_index(), entry());
  }

  uint32_t entry_index() {
    FTL_DCHECK(entry_) << "entry_index called outside of HandleMessage";
    return entry_index_;
  }

  const FlogEntryPtr& entry() {
    FTL_DCHECK(entry_) << "entry called outside of HandleMessage";
    return *entry_;
  }

  std::shared_ptr<Channel> AsChannel(uint64_t subject_address);

  void BindAs(uint64_t koid);

  void SetBindingKoid(Binding* binding, uint64_t koid);

 private:
  ChannelManager* manager_;

  // These fields are only used during calls to HandleMessage().
  std::shared_ptr<Channel> channel_ = nullptr;
  uint32_t entry_index_ = 0;
  const FlogEntryPtr* entry_ = nullptr;
};

}  // namespace flog
