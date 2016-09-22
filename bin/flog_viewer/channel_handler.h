// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_TOOLS_FLOG_VIEWER_CHANNEL_HANDLER_H_
#define APPS_MEDIA_TOOLS_FLOG_VIEWER_CHANNEL_HANDLER_H_

#include "apps/media/interfaces/flog/flog.mojom.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/message.h"

namespace mojo {
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
  using ChannelLookupCallback =
      std::function<std::shared_ptr<Channel>(uint64_t)>;

  static std::unique_ptr<ChannelHandler> Create(const std::string& type_name,
                                                const std::string& format);

  virtual ~ChannelHandler();

  // Handles a channel message.
  void HandleMessage(uint32_t entry_index,
                     const FlogEntryPtr& entry,
                     Message* message,
                     const ChannelLookupCallback& channel_lookup_callback);

  // Gets the accumulator from the handler, if there is one. The default
  // implementation returns a null pointer.
  virtual std::shared_ptr<Accumulator> GetAccumulator();

 protected:
  ChannelHandler();

  virtual void HandleMessage(Message* message) = 0;

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

 private:
  // These fields are only used during calls to HandleMessage().
  uint32_t entry_index_;
  const FlogEntryPtr* entry_;
  ChannelLookupCallback channel_lookup_callback_;
};

}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_TOOLS_FLOG_VIEWER_CHANNEL_HANDLER_H_
