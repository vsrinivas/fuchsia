// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iostream>
#include <streambuf>

#include "apps/media/services/flog/flog.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/binding.h"
#include "apps/media/tools/flog_viewer/channel_manager.h"
#include "lib/fidl/cpp/bindings/message.h"
#include "lib/ftl/logging.h"

namespace flog {

class Channel;

template <class cT, class traits = std::char_traits<cT>>
class basic_nullbuf : public std::basic_streambuf<cT, traits> {
  typename traits::int_type overflow(typename traits::int_type c) {
    return traits::not_eof(c);  // indicate success
  }
};

template <class cT, class traits = std::char_traits<cT>>
class basic_onullstream : public std::basic_ostream<cT, traits> {
 public:
  basic_onullstream()
      : std::basic_ios<cT, traits>(&sbuf_),
        std::basic_ostream<cT, traits>(&sbuf_) {
    std::basic_ostream<cT, traits>::init(&sbuf_);
  }

 private:
  basic_nullbuf<cT, traits> sbuf_;
};

typedef basic_onullstream<char> onullstream;

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
  static const std::string kFormatTerse;
  static const std::string kFormatFull;
  static const std::string kFormatDigest;

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
  ChannelHandler(const std::string& format);

  virtual void HandleMessage(fidl::Message* message) = 0;

  std::ostream& ReportProblem() {
    FTL_DCHECK(entry_) << "ReportProblem called outside of HandleMessage";
    return GetAccumulator()->ReportProblem(entry_index(), entry());
  }

  uint32_t entry_index() const {
    FTL_DCHECK(entry_) << "entry_index called outside of HandleMessage";
    return entry_index_;
  }

  const FlogEntryPtr& entry() const {
    FTL_DCHECK(entry_) << "entry called outside of HandleMessage";
    return *entry_;
  }

  std::shared_ptr<Channel> AsChannel(uint64_t subject_address);

  void BindAs(uint64_t koid);

  void SetBindingKoid(Binding* binding, uint64_t koid);

  const std::string& format() const { return format_; }

  std::ostream& full_out() {
    return format_ == kFormatFull ? std::cout : onull;
  }

  std::ostream& terse_out() {
    return format_ != kFormatDigest ? std::cout : onull;
  }

 private:
  ChannelManager* manager_;
  std::string format_;

  // These fields are only used during calls to HandleMessage().
  std::shared_ptr<Channel> channel_ = nullptr;
  uint32_t entry_index_ = 0;
  const FlogEntryPtr* entry_ = nullptr;
  onullstream onull;
};

}  // namespace flog
