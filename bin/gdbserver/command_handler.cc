// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_handler.h"

#include <string>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

#include "server.h"
#include "util.h"

namespace debugserver {

namespace {

// TODO(armansito): Update this as we add more features.
const char kSupportedFeatures[] = "QNonStop+;";

const char kFirstThreadInfo[] = "fThreadInfo";
const char kNonStop[] = "NonStop";
const char kSubsequentThreadInfo[] = "sThreadInfo";
const char kSupported[] = "Supported";

void ReplyOK(const CommandHandler::ResponseCallback& callback) {
  callback("OK");
}

void ReplyWithError(util::ErrorCode error_code,
                    const CommandHandler::ResponseCallback& callback) {
  std::string error_rsp = util::BuildErrorPacket(error_code);
  callback(error_rsp);
}

}  // namespace

CommandHandler::CommandHandler(Server* server)
    : server_(server), in_thread_info_sequence_(false) {
  FTL_DCHECK(server_);
}

bool CommandHandler::HandleCommand(const ftl::StringView& packet,
                                   const ResponseCallback& callback) {
  // GDB packets are prefixed with a letter that maps to a particular command
  // "family". We do the initial multiplexing here and let each individual
  // sub-handler deal with the rest.
  if (packet.empty()) {
    // TODO(armansito): Is there anything meaningful that we can do here?
    FTL_LOG(ERROR) << "Empty packet received";
    return false;
  }

  switch (packet[0]) {
    case '?':  // Indicate the reason the target halted
      return HandleQuestionMark(callback);
    case 'H':  // Set a thread for subsequent operations
      return Handle_H(packet.substr(1), callback);
    case 'q':  // General query packet
    case 'Q':  // General set packet
    {
      ftl::StringView prefix, params;
      util::ExtractParameters(packet.substr(1), &prefix, &params);

      if (packet[0] == 'q')
        return Handle_q(prefix, params, callback);
      return Handle_Q(prefix, params, callback);
    }
    default:
      break;
  }

  return false;
}

bool CommandHandler::HandleQuestionMark(const ResponseCallback& callback) {
  // TODO(armansito): Implement this once we actually listen to thread/process
  // exceptions. The logic for NonStop mode is fairly simple:
  //    1. Tell Server to drop any pending and/or queued Stop Reply
  //    notifications.
  //
  //    2. Go through all processes and send a notification for the status of
  //    each.
  //
  //    3. If there is no inferior or the current inferior is not started, then
  //    reply "OK".
  ReplyOK(callback);
  return true;
}

bool CommandHandler::Handle_H(const ftl::StringView& packet,
                              const ResponseCallback& callback) {
  // Here we set the "current thread" for subsequent operations
  // (‘m’, ‘M’, ‘g’, ‘G’, et.al.).
  // There are two types of an H packet. 'c' and 'g'. We claim to not support
  // 'c' because it's specified as deprecated.

  // Packet should at least contain 'c' or 'g' and some characters for the
  // thread id.
  if (packet.size() < 2) {
    ReplyWithError(util::ErrorCode::INVAL, callback);
    return true;
  }

  switch (packet[0]) {
    case 'c':
      FTL_LOG(ERROR) << "Not handling deprecated H packet type";
      return false;
    case 'g': {
      int64_t pid, tid;
      bool has_pid;
      if (!util::ParseThreadId(packet.substr(1), &has_pid, &pid, &tid)) {
        ReplyWithError(util::ErrorCode::INVAL, callback);
        return true;
      }

      // We currently support debugging only one process.
      // TODO(armansito): What to do with a process ID? Replying with an empty
      // packet for now.
      if (has_pid) {
        FTL_LOG(WARNING)
            << "Specifying a pid while setting the current thread is"
            << " not supported";
        return false;
      }

      // Setting the current thread to "all threads" doesn't make much sense.
      if (tid < 0) {
        FTL_LOG(WARNING) << "Cannot set the current thread to all threads";
        ReplyWithError(util::ErrorCode::INVAL, callback);
        return true;
      }

      if (!server_->current_process()) {
        FTL_LOG(WARNING) << "No inferior exists";

        // If we're given a positive thread ID but there is currently no
        // inferior,
        // then report error?
        if (!tid) {
          FTL_LOG(ERROR) << "Cannot set a current thread with no inferior";
          ReplyWithError(util::ErrorCode::PERM, callback);
          return true;
        }

        FTL_LOG(WARNING) << "Setting current thread to NULL for tid=0";

        server_->SetCurrentThread(nullptr);
        ReplyOK(callback);
        return true;
      }

      // If the process hasn't started yet it will have no threads. Since "Hg0"
      // is
      // one of the first things that GDB sends after a connection (and since we
      // don't run the process right away), we lie to GDB and set the current
      // thread to null.
      if (!server_->current_process()->started()) {
        FTL_LOG(INFO) << "Current process has no threads yet but we pretend to "
                      << "set one";
        server_->SetCurrentThread(nullptr);
        ReplyOK(callback);
        return true;
      }

      Thread* thread = nullptr;

      // A thread ID value of 0 means "pick an arbitrary thread".
      if (tid == 0)
        thread = server_->current_process()->PickOneThread();
      else
        thread = server_->current_process()->FindThreadById(tid);

      if (!thread) {
        FTL_LOG(ERROR) << "Failed to set the current thread";
        ReplyWithError(util::ErrorCode::PERM, callback);
        return true;
      }

      server_->SetCurrentThread(thread);
      ReplyOK(callback);

      return true;
    }
    default:
      break;
  }

  return false;
}

bool CommandHandler::Handle_q(const ftl::StringView& prefix,
                              const ftl::StringView& params,
                              const ResponseCallback& callback) {
  if (prefix == kSupported)
    return HandleQuerySupported(params, callback);
  if (prefix == kFirstThreadInfo)
    return HandleQueryThreadInfo(true, callback);
  if (prefix == kSubsequentThreadInfo)
    return HandleQueryThreadInfo(false, callback);

  return false;
}

bool CommandHandler::Handle_Q(const ftl::StringView& prefix,
                              const ftl::StringView& params,
                              const ResponseCallback& callback) {
  if (prefix == kNonStop)
    return HandleSetNonStop(params, callback);

  return false;
}

bool CommandHandler::HandleQuerySupported(const ftl::StringView& params,
                                          const ResponseCallback& callback) {
  // We ignore the parameters for qSupported. Respond with the supported
  // features.
  callback(kSupportedFeatures);
  return true;
}

bool CommandHandler::HandleSetNonStop(const ftl::StringView& params,
                                      const ResponseCallback& callback) {
  // The only values we accept are "1" and "0".
  if (params.size() != 1) {
    ReplyWithError(util::ErrorCode::INVAL, callback);
    return true;
  }

  // We currently only support non-stop mode.
  char value = params[0];
  if (value == '1') {
    ReplyOK(callback);
  } else if (value == '0') {
    ReplyWithError(util::ErrorCode::PERM, callback);
  } else {
    FTL_LOG(ERROR) << "QNonStop received with invalid value: "
                   << (unsigned)value;
    ReplyWithError(util::ErrorCode::INVAL, callback);
  }

  return true;
}

bool CommandHandler::HandleQueryThreadInfo(bool is_first,
                                           const ResponseCallback& callback) {
  FTL_DCHECK(server_);

  Process* current_process = server_->current_process();
  if (!current_process) {
    FTL_LOG(ERROR) << "Current process is not set";
    ReplyWithError(util::ErrorCode::PERM, callback);
    return true;
  }

  // For the "first" thread info query we reply with the complete list of
  // threads and always report "end of list" for subsequent queries. The GDB
  // Remote Protocol does not seem to define a MTU, however, we could be running
  // on a platform with resource constraints that may require us to break up the
  // sequence into multiple packets. For now we do not worry about this.

  if (!is_first) {
    // This is a subsequent query. Check that a thread info query sequence was
    // started (just for sanity) and report end of list.
    if (!in_thread_info_sequence_) {
      FTL_LOG(ERROR) << "qsThreadInfo received without first receiving "
                     << "qfThreadInfo";
      ReplyWithError(util::ErrorCode::PERM, callback);
      return true;
    }

    in_thread_info_sequence_ = false;
    callback("l");
    return true;
  }

  // This is the first query. Check the sequence state for sanity.
  if (in_thread_info_sequence_) {
    FTL_LOG(ERROR) << "qfThreadInfo received while already in an active "
                   << "sequence";
    ReplyWithError(util::ErrorCode::PERM, callback);
    return true;
  }

  std::deque<std::string> thread_ids;
  size_t buf_size = 0;
  current_process->ForEachThread([&thread_ids, &buf_size](Thread* thread) {
    std::string thread_id = ftl::NumberToString<mx_koid_t>(thread->thread_id());
    buf_size += thread_id.length();
    thread_ids.push_back(thread_id);
  });

  if (thread_ids.empty()) {
    // No ids to report. End of sequence.
    callback("l");
    return true;
  }

  in_thread_info_sequence_ = true;

  // Add the number of commas (|thread_ids.size() - 1|) plus the prefix "m")
  buf_size += thread_ids.size();

  std::unique_ptr<char[]> buffer(new char[buf_size]);
  buffer.get()[0] = 'm';
  util::JoinStrings(thread_ids, ',', buffer.get() + 1, buf_size - 1);

  callback(ftl::StringView(buffer.get(), buf_size));

  return true;
}

}  // namespace debugserver
