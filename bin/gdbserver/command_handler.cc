// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_handler.h"

#include <string>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

#include "registers.h"
#include "server.h"
#include "thread.h"
#include "util.h"

namespace debugserver {

namespace {

// TODO(armansito): Update this as we add more features.
const char kSupportedFeatures[] = "QNonStop+;";

const char kAttached[] = "Attached";
const char kCurrentThreadId[] = "C";
const char kFirstThreadInfo[] = "fThreadInfo";
const char kNonStop[] = "NonStop";
const char kRun[] = "Run;";
const char kSubsequentThreadInfo[] = "sThreadInfo";
const char kSupported[] = "Supported";

// This always returns true so that command handlers can simple call "return
// ReplyOK()" rather than "ReplyOK(); return true;
bool ReplyOK(const CommandHandler::ResponseCallback& callback) {
  callback("OK");
  return true;
}

// This always returns true so that command handlers can simple call "return
// ReplyWithError()" rather than "ReplyWithError(); return true;
bool ReplyWithError(util::ErrorCode error_code,
                    const CommandHandler::ResponseCallback& callback) {
  std::string error_rsp = util::BuildErrorPacket(error_code);
  callback(error_rsp);
  return true;
}

// Returns true if |str| starts with |prefix|.
bool StartsWith(const ftl::StringView& str, const ftl::StringView& prefix) {
  return str.substr(0, prefix.size()) == prefix;
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
      if (packet.size() > 1)
        break;
      return HandleQuestionMark(callback);
    case 'g':  // Read general registers
      if (packet.size() > 1)
        break;
      return Handle_g(callback);
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
    case 'v':
      return Handle_v(packet.substr(1), callback);
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
  return ReplyOK(callback);
}

bool CommandHandler::Handle_g(const ResponseCallback& callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FTL_LOG(ERROR) << "g: No inferior";
    return ReplyWithError(util::ErrorCode::INVAL, callback);
  }

  // If there is no current thread, then we reply with "0"s for all registers.
  std::string result;
  if (!server_->current_thread()) {
    result = arch::Registers::GetUninitializedGeneralRegisters();
  } else {
    arch::Registers* regs = server_->current_thread()->registers();
    FTL_DCHECK(regs);
    result = regs->GetGeneralRegisters();
  }

  if (result.empty()) {
    FTL_LOG(ERROR) << "g: Failed to read register values";
    return ReplyWithError(util::ErrorCode::PERM, callback);
  }

  callback(result);
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
  if (packet.size() < 2)
    return ReplyWithError(util::ErrorCode::INVAL, callback);

  switch (packet[0]) {
    case 'c':
      FTL_LOG(ERROR) << "Not handling deprecated H packet type";
      return false;
    case 'g': {
      int64_t pid, tid;
      bool has_pid;
      if (!util::ParseThreadId(packet.substr(1), &has_pid, &pid, &tid))
        return ReplyWithError(util::ErrorCode::INVAL, callback);

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
        return ReplyWithError(util::ErrorCode::INVAL, callback);
      }

      if (!server_->current_process()) {
        FTL_LOG(WARNING) << "No inferior exists";

        // If we're given a positive thread ID but there is currently no
        // inferior,
        // then report error?
        if (!tid) {
          FTL_LOG(ERROR) << "Cannot set a current thread with no inferior";
          return ReplyWithError(util::ErrorCode::PERM, callback);
        }

        FTL_LOG(WARNING) << "Setting current thread to NULL for tid=0";

        server_->SetCurrentThread(nullptr);
        return ReplyOK(callback);
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
        return ReplyOK(callback);
      }

      Thread* thread = nullptr;

      // A thread ID value of 0 means "pick an arbitrary thread".
      if (tid == 0)
        thread = server_->current_process()->PickOneThread();
      else
        thread = server_->current_process()->FindThreadById(tid);

      if (!thread) {
        FTL_LOG(ERROR) << "Failed to set the current thread";
        return ReplyWithError(util::ErrorCode::PERM, callback);
      }

      server_->SetCurrentThread(thread);
      return ReplyOK(callback);
    }
    default:
      break;
  }

  return false;
}

bool CommandHandler::Handle_q(const ftl::StringView& prefix,
                              const ftl::StringView& params,
                              const ResponseCallback& callback) {
  if (prefix == kAttached)
    return HandleQueryAttached(params, callback);

  if (prefix == kCurrentThreadId)
    return HandleQueryCurrentThreadId(params, callback);

  if (prefix == kFirstThreadInfo)
    return HandleQueryThreadInfo(true, callback);

  if (prefix == kSubsequentThreadInfo)
    return HandleQueryThreadInfo(false, callback);

  if (prefix == kSupported)
    return HandleQuerySupported(params, callback);

  return false;
}

bool CommandHandler::Handle_Q(const ftl::StringView& prefix,
                              const ftl::StringView& params,
                              const ResponseCallback& callback) {
  if (prefix == kNonStop)
    return HandleSetNonStop(params, callback);

  return false;
}

bool CommandHandler::Handle_v(const ftl::StringView& packet,
                              const ResponseCallback& callback) {
  if (StartsWith(packet, kRun))
    return Handle_vRun(packet.substr(std::strlen(kRun)), callback);

  return false;
}

bool CommandHandler::HandleQueryAttached(const ftl::StringView& params,
                                         const ResponseCallback& callback) {
  // We don't support multiprocessing yet, so make sure we received the version
  // of qAttached that doesn't have a "pid" parameter.
  if (!params.empty())
    return ReplyWithError(util::ErrorCode::INVAL, callback);

  // The response is "1" if we attached to an existing process, or "0" if we
  // created a new one. We currently don't support the former, so always send
  // "0".
  callback("0");
  return true;
}

bool CommandHandler::HandleQueryCurrentThreadId(
    const ftl::StringView& params,
    const ResponseCallback& callback) {
  // The "qC" packet has no parameters.
  if (!params.empty())
    return ReplyWithError(util::ErrorCode::INVAL, callback);

  Thread* current_thread = server_->current_thread();
  if (!current_thread) {
    // If there is a current process and it has been started, pick one thread
    // and set that as the current one. This is our work around for lying to GDB
    // about setting a current thread in response to an early Hg0 packet.
    Process* current_process = server_->current_process();
    if (!current_process || !current_process->started()) {
      FTL_LOG(ERROR) << "qC: Current thread has not been set";
      return ReplyWithError(util::ErrorCode::PERM, callback);
    }

    FTL_VLOG(1) << "qC: Picking one arbitrary thread";
    current_thread = current_process->PickOneThread();
    if (!current_thread) {
      FTL_VLOG(1) << "qC: Failed to pick a thread";
      return ReplyWithError(util::ErrorCode::PERM, callback);
    }
  }

  std::string thread_id = ftl::NumberToString<mx_koid_t>(
      current_thread->thread_id(), ftl::Base::k16);

  std::string reply = "QC" + thread_id;
  callback(reply);
  return true;
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
  if (params.size() != 1)
    return ReplyWithError(util::ErrorCode::INVAL, callback);

  // We currently only support non-stop mode.
  char value = params[0];
  if (value == '1')
    return ReplyOK(callback);

  if (value == '0')
    return ReplyWithError(util::ErrorCode::PERM, callback);

  FTL_LOG(ERROR) << "QNonStop received with invalid value: " << (unsigned)value;
  return ReplyWithError(util::ErrorCode::INVAL, callback);
}

bool CommandHandler::HandleQueryThreadInfo(bool is_first,
                                           const ResponseCallback& callback) {
  FTL_DCHECK(server_);

  Process* current_process = server_->current_process();
  if (!current_process) {
    FTL_LOG(ERROR) << "Current process is not set";
    return ReplyWithError(util::ErrorCode::PERM, callback);
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
      return ReplyWithError(util::ErrorCode::PERM, callback);
    }

    in_thread_info_sequence_ = false;
    callback("l");
    return true;
  }

  // This is the first query. Check the sequence state for sanity.
  if (in_thread_info_sequence_) {
    FTL_LOG(ERROR) << "qfThreadInfo received while already in an active "
                   << "sequence";
    return ReplyWithError(util::ErrorCode::PERM, callback);
  }

  std::deque<std::string> thread_ids;
  size_t buf_size = 0;
  current_process->ForEachThread([&thread_ids, &buf_size](Thread* thread) {
    std::string thread_id =
        ftl::NumberToString<mx_koid_t>(thread->thread_id(), ftl::Base::k16);
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

bool CommandHandler::Handle_vRun(const ftl::StringView& packet,
                                 const ResponseCallback& callback) {
  // TODO(armansito): We're keeping it simple for now always only run the
  // program that was passed to gdbserver in the command-line. Fix this later.
  if (!packet.empty()) {
    FTL_LOG(ERROR) << "vRun: Only running the default program is supported";
    return ReplyWithError(util::ErrorCode::INVAL, callback);
  }

  Process* current_process = server_->current_process();
  if (!current_process) {
    FTL_LOG(ERROR) << "vRun: no current process to run!";
    return ReplyWithError(util::ErrorCode::PERM, callback);
  }

  if (!current_process->IsAttached() && !current_process->Attach()) {
    FTL_LOG(ERROR) << "vRun: Failed to attach process!";
    return ReplyWithError(util::ErrorCode::PERM, callback);
  }

  // On Linux, the program is considered "live" after vRun, e.g. $pc is set. On
  // magenta $pc isn't set until the call to launchpad_start (i.e.
  // debugserver::Process::Start()), however we cannot call that here as a
  // response to vRun since the program should be created in the "stopped
  // state". We simply make sure that the process is attached and leave it at
  // that.
  //
  // TODO(armansito|dje): Should this be changed in Magenta, so that $pc is set
  // before calling launchpad_start?
  FTL_DCHECK(current_process->IsAttached());

  // In Remote Non-stop mode (which is the only mode we currently support), we
  // just respond "OK" (see
  // https://sourceware.org/gdb/current/onlinedocs/gdb/Stop-Reply-Packets.html)
  return ReplyOK(callback);
}

}  // namespace debugserver
