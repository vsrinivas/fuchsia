// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cmd_handler.h"

#include <algorithm>
#include <cinttypes>
#include <string>

#include "garnet/lib/debugger_utils/util.h"

#include "garnet/lib/inferior_control/registers.h"
#include "garnet/lib/inferior_control/thread.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/split_string.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "lib/fxl/strings/string_printf.h"

#include "server.h"
#include "thread_action_list.h"
#include "util.h"

namespace debugserver {

namespace {

const char kSupportedFeatures[] =
    "QNonStop+;"
#if 0  // TODO(dje)
  "QThreadEvents+;"
#endif
#if 0  // TODO(dje)
  "swbreak+;"
#endif
    "qXfer:auxv:read+";

const char kAttached[] = "Attached";
const char kCurrentThreadId[] = "C";
const char kFirstThreadInfo[] = "fThreadInfo";
const char kNonStop[] = "NonStop";
const char kRcmd[] = "Rcmd,";
const char kSubsequentThreadInfo[] = "sThreadInfo";
const char kSupported[] = "Supported";
const char kXfer[] = "Xfer";

// v Commands
const char kAttach[] = "Attach;";
const char kCont[] = "Cont;";
const char kKill[] = "Kill;";
const char kRun[] = "Run;";

// qRcmd commands
const char kExit[] = "exit";
const char kHelp[] = "help";
const char kQuit[] = "quit";
const char kSet[] = "set";
const char kShow[] = "show";

// This always returns true so that command handlers can simple call "return
// ReplyOK()" rather than "ReplyOK(); return true;
bool ReplyOK(CommandHandler::ResponseCallback callback) {
  callback("OK");
  return true;
}

// This always returns true so that command handlers can simple call "return
// ReplyWithError()" rather than "ReplyWithError(); return true;
bool ReplyWithError(ErrorCode error_code,
                    CommandHandler::ResponseCallback callback) {
  std::string error_rsp = BuildErrorPacket(error_code);
  callback(error_rsp);
  return true;
}

// Returns true if |str| starts with |prefix|.
bool StartsWith(const fxl::StringView& str, const fxl::StringView& prefix) {
  return str.substr(0, prefix.size()) == prefix;
}

std::vector<std::string> BuildArgvFor_vRun(const fxl::StringView& packet) {
  std::vector<std::string> argv;
  size_t len = packet.size();
  size_t s = 0;

  while (s < len) {
    size_t semi = packet.find(';', s);
    size_t n;
    if (semi == fxl::StringView::npos)
      n = len - s;
    else
      n = semi - s;
    std::vector<uint8_t> arg = DecodeByteArrayString(packet.substr(s, n));
    auto char_arg = reinterpret_cast<char*>(arg.data());
    argv.push_back(std::string(char_arg, arg.size()));
    if (semi == fxl::StringView::npos)
      s = len;
    else
      s = semi + 1;
  }

  return argv;
}

}  // namespace

CommandHandler::CommandHandler(RspServer* server)
    : server_(server), in_thread_info_sequence_(false) {
  FXL_DCHECK(server_);
}

bool CommandHandler::HandleCommand(const fxl::StringView& packet,
                                   ResponseCallback callback) {
  // GDB packets are prefixed with a letter that maps to a particular command
  // "family". We do the initial multiplexing here and let each individual
  // sub-handler deal with the rest.
  if (packet.empty()) {
    // TODO(armansito): Is there anything meaningful that we can do here?
    FXL_LOG(ERROR) << "Empty packet received";
    return false;
  }

  switch (packet[0]) {
    case '?':  // Indicate the reason the target halted
      if (packet.size() > 1)
        break;
      return HandleQuestionMark(std::move(callback));
    case 'c':  // Continue (at addr)
      return Handle_c(packet.substr(1), std::move(callback));
    case 'C':  // Continue with signal (optionally at addr)
      return Handle_C(packet.substr(1), std::move(callback));
    case 'D':  // Detach
      return Handle_D(packet.substr(1), std::move(callback));
    case 'g':  // Read general registers
      if (packet.size() > 1)
        break;
      return Handle_g(std::move(callback));
    case 'G':  // Write general registers
      return Handle_G(packet.substr(1), std::move(callback));
    case 'H':  // Set a thread for subsequent operations
      return Handle_H(packet.substr(1), std::move(callback));
    case 'm':  // Read memory
      return Handle_m(packet.substr(1), std::move(callback));
    case 'M':  // Write memory
      return Handle_M(packet.substr(1), std::move(callback));
    case 'q':  // General query packet
    case 'Q':  // General set packet
    {
      fxl::StringView prefix, params;
      ExtractParameters(packet.substr(1), &prefix, &params);

      FXL_VLOG(1) << "\'" << packet[0] << "\' packet - prefix: " << prefix
                  << ", params: " << params;

      if (packet[0] == 'q')
        return Handle_q(prefix, params, std::move(callback));
      return Handle_Q(prefix, params, std::move(callback));
    }
    case 'T':  // Is thread alive?
      return Handle_T(packet.substr(1), std::move(callback));
    case 'v':  // v-packets
      return Handle_v(packet.substr(1), std::move(callback));
    case 'z':  // Remove software breakpoint
    case 'Z':  // Insert software breakpoint
      return Handle_zZ(packet[0] == 'Z', packet.substr(1), std::move(callback));
    default:
      break;
  }

  return false;
}

bool CommandHandler::HandleQuestionMark(ResponseCallback callback) {
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
  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_c(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "c: No inferior";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  Thread* current_thread = server_->current_thread();

  // If the packet contains an address parameter, then try to set the program
  // counter to then continue at that address. Otherwise, the PC register will
  // remain untouched.
  zx_vaddr_t addr;
  if (!packet.empty()) {
    if (!fxl::StringToNumberWithError<zx_vaddr_t>(packet, &addr,
                                                  fxl::Base::k16)) {
      FXL_LOG(ERROR) << "c: Malformed address given: " << packet;
      return ReplyWithError(ErrorCode::INVAL, std::move(callback));
    }

    // If there is no current thread, then report error. This is a special case
    // that means that the process hasn't started yet.
    if (!current_thread) {
      FXL_DCHECK(!current_process->IsLive());
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }

    if (!current_thread->registers()->RefreshGeneralRegisters()) {
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }
    if (!current_thread->registers()->SetRegister(GetPCRegisterNumber(), &addr,
                                                  sizeof(addr))) {
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }
    if (!current_thread->registers()->WriteGeneralRegisters()) {
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }

    // TODO(armansito): Restore the PC register to its original state in case of
    // a failure condition below?
  }

  // If there is a current thread, then tell it to continue.
  if (current_thread) {
    if (!current_thread->Resume())
      return ReplyWithError(ErrorCode::PERM, std::move(callback));

    return ReplyOK(std::move(callback));
  }

  // There is no current thread. This means that the process hasn't been started
  // yet. We start it and set the current thread to the first one the kernel
  // gives us.
  // TODO(armansito): Remove this logic now that we handle
  // ZX_EXCP_THREAD_STARTING?
  FXL_DCHECK(!current_process->IsLive());
  if (!current_process->Start()) {
    FXL_LOG(ERROR) << "c: Failed to start the current inferior";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // Try to set the current thread.
  // TODO(armansito): Can this be racy?
  current_thread = current_process->PickOneThread();
  if (current_thread)
    server_->SetCurrentThread(current_thread);

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_C(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "C: No inferior";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  Thread* current_thread = server_->current_thread();
  if (!current_thread) {
    FXL_LOG(ERROR) << "C: No current thread";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // Parse the parameters. The packet format is: sig[;addr]
  size_t semicolon = packet.find(';');
  if (semicolon == fxl::StringView::npos)
    semicolon = packet.size();

  int signo;
  if (!fxl::StringToNumberWithError<int>(packet.substr(0, semicolon), &signo,
                                         fxl::Base::k16)) {
    FXL_LOG(ERROR) << "C: Malformed packet: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  GdbSignal thread_signo = current_thread->GetGdbSignal();
  // TODO(dje): kNone may be a better value to use here.
  if (thread_signo == GdbSignal::kUnsupported) {
    FXL_LOG(ERROR) << "C: Current thread has received no signal";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }
  int int_thread_signo = static_cast<int>(thread_signo);

  if (int_thread_signo != signo) {
    FXL_LOG(ERROR) << "C: Signal numbers don't match - actual: "
                   << int_thread_signo << ", received: " << signo;
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  auto addr_param = packet.substr(semicolon);

  // If the packet contains an address parameter, then try to set the program
  // counter to then continue at that address. Otherwise, the PC register will
  // remain untouched.
  // TODO(armansito): Make Thread::Resume take an optional address argument so
  // we don't have to keep repeating this code.
  if (!addr_param.empty()) {
    zx_vaddr_t addr;
    if (!fxl::StringToNumberWithError<zx_vaddr_t>(addr_param, &addr,
                                                  fxl::Base::k16)) {
      FXL_LOG(ERROR) << "C: Malformed address given: " << packet;
      return ReplyWithError(ErrorCode::INVAL, std::move(callback));
    }

    if (!current_thread->registers()->RefreshGeneralRegisters()) {
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }
    if (!current_thread->registers()->SetRegister(GetPCRegisterNumber(), &addr,
                                                  sizeof(addr))) {
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }
    if (!current_thread->registers()->WriteGeneralRegisters()) {
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }

    // TODO(armansito): Restore the PC register to its original state in case of
    // a failure condition below?
  }

  if (!current_thread->Resume()) {
    FXL_LOG(ERROR) << "Failed to resume thread";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_D(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "D: No inferior";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // For now we only support detaching from the one process we have.
  if (packet[0] == ';') {
    zx_koid_t pid;
    if (!fxl::StringToNumberWithError<zx_koid_t>(packet.substr(1), &pid,
                                                 fxl::Base::k16)) {
      FXL_LOG(ERROR) << "D: bad pid: " << packet;
      return ReplyWithError(ErrorCode::INVAL, std::move(callback));
    }
    if (pid != current_process->id()) {
      FXL_LOG(ERROR) << "D: unknown pid: " << pid;
      return ReplyWithError(ErrorCode::INVAL, std::move(callback));
    }
  } else if (packet != "") {
    FXL_LOG(ERROR) << "D: Malformed packet: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  if (!current_process->IsAttached()) {
    FXL_LOG(ERROR) << "D: Not attached to process " << current_process->id();
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  if (!current_process->Detach()) {
    // At the moment this shouldn't happen, but we don't want to kill the
    // debug session because of it. The details of the failure are already
    // logged by Detach().
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }
  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_g(ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "g: No inferior";
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  // If there is no current thread, then we reply with "0"s for all registers.
  // TODO(armansito): gG packets are technically used to read/write "ALL"
  // registers, not just the general registers. We'll have to take this into
  // account in the future, though for now we're just supporting general
  // registers.
  std::string result;
  if (!server_->current_thread()) {
    result = Registers::GetUninitializedGeneralRegistersAsString();
  } else {
    Registers* regs = server_->current_thread()->registers();
    FXL_DCHECK(regs);
    result = regs->GetGeneralRegistersAsString();
  }

  if (result.empty()) {
    FXL_LOG(ERROR) << "g: Failed to read register values";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  callback(result);
  return true;
}

bool CommandHandler::Handle_G(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "G: No inferior";
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  // If there is no current thread report an error.
  Thread* current_thread = server_->current_thread();
  if (!current_thread) {
    FXL_LOG(ERROR) << "G: No current thread";
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  // We pass the packet here directly since Registers handles the parsing.
  // TODO(armansito): gG packets are technically used to read/write "ALL"
  // registers, not just the general registers. We'll have to take this into
  // account in the future, though for now we're just supporting general
  // registers.
  if (!current_thread->registers()->SetGeneralRegistersFromString(packet)) {
    FXL_LOG(ERROR) << "G: Failed to write to general registers";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }
  if (!current_thread->registers()->WriteGeneralRegisters()) {
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_H(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // Here we set the "current thread" for subsequent operations
  // (‘m’, ‘M’, ‘g’, ‘G’, et.al.).
  // There are two types of an H packet. 'c' and 'g'. We claim to not support
  // 'c' because it's specified as deprecated.

  // Packet should at least contain 'c' or 'g' and some characters for the
  // thread id.
  if (packet.size() < 2)
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));

  switch (packet[0]) {
    case 'c':  // fall through
    case 'g': {
      int64_t pid, tid;
      bool has_pid;
      if (!ParseThreadId(packet.substr(1), &has_pid, &pid, &tid))
        return ReplyWithError(ErrorCode::INVAL, std::move(callback));

      // We currently support debugging only one process.
      // TODO(armansito): What to do with a process ID? Replying with an empty
      // packet for now.
      if (has_pid) {
        FXL_LOG(WARNING)
            << "Specifying a pid while setting the current thread is"
            << " not supported";
        return false;
      }

      // Setting the current thread to "all threads" doesn't make much sense.
      if (tid < 0) {
        FXL_LOG(ERROR) << "Cannot set the current thread to all threads";
        return ReplyWithError(ErrorCode::INVAL, std::move(callback));
      }

      Process* current_process = server_->current_process();

      // Note that at this point we may have a process but are not necessarily
      // attached yet. GDB sends the Hg0 packet early on, and expects it to
      // succeed.
      if (!current_process) {
        FXL_LOG(ERROR) << "No inferior exists";

        // If we're given a positive thread ID but there is currently no
        // inferior, then report error?
        if (!tid) {
          FXL_LOG(ERROR) << "Cannot set a current thread with no inferior";
          return ReplyWithError(ErrorCode::PERM, std::move(callback));
        }

        FXL_LOG(WARNING) << "Setting current thread to NULL for tid=0";

        server_->SetCurrentThread(nullptr);
        return ReplyOK(std::move(callback));
      }

      // If the process hasn't started yet it will have no threads. Since "Hg0"
      // is one of the first things that GDB sends after a connection (and
      // since we don't run the process right away), we lie to GDB and set the
      // current thread to null.
      if (!current_process->IsLive()) {
        FXL_LOG(INFO) << "Current process has no threads yet but we pretend to "
                      << "set one";
        server_->SetCurrentThread(nullptr);
        return ReplyOK(std::move(callback));
      }

      current_process->EnsureThreadMapFresh();

      Thread* thread;

      // A thread ID value of 0 means "pick an arbitrary thread".
      if (tid == 0)
        thread = current_process->PickOneThread();
      else
        thread = current_process->FindThreadById(tid);

      if (!thread) {
        FXL_LOG(ERROR) << "Failed to set the current thread";
        return ReplyWithError(ErrorCode::PERM, std::move(callback));
      }

      server_->SetCurrentThread(thread);
      return ReplyOK(std::move(callback));
    }
    default:
      break;
  }

  return false;
}

bool CommandHandler::Handle_m(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "m: No inferior";
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  // The "m" packet should have two arguments for addr and length, separated by
  // a single comma.
  auto params = fxl::SplitString(packet, ",", fxl::kKeepWhitespace,
                                 fxl::kSplitWantNonEmpty);
  if (params.size() != 2) {
    FXL_LOG(ERROR) << "m: Malformed packet: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  uintptr_t addr;
  size_t length;
  if (!fxl::StringToNumberWithError<uintptr_t>(params[0], &addr,
                                               fxl::Base::k16) ||
      !fxl::StringToNumberWithError<size_t>(params[1], &length,
                                            fxl::Base::k16)) {
    FXL_LOG(ERROR) << "m: Malformed params: " << packet;
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  std::unique_ptr<uint8_t[]> buffer(new uint8_t[length]);
  if (!current_process->ReadMemory(addr, buffer.get(), length)) {
    FXL_LOG(ERROR) << "m: Failed to read memory";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  std::string result = EncodeByteArrayString(buffer.get(), length);
  callback(result);
  return true;
}

bool CommandHandler::Handle_M(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "M: No inferior";
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  // The "M" packet parameters look like this: "addr,length:XX...".
  // First, extract the addr,len and data sections. Using fxl::kSplitWantAll
  // here since the data portion could technically be empty if the given length
  // is 0.
  auto params =
      fxl::SplitString(packet, ":", fxl::kKeepWhitespace, fxl::kSplitWantAll);
  if (params.size() != 2) {
    FXL_LOG(ERROR) << "M: Malformed packet: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  fxl::StringView data = params[1];

  // Extract addr and len
  params = fxl::SplitString(params[0], ",", fxl::kKeepWhitespace,
                            fxl::kSplitWantNonEmpty);
  if (params.size() != 2) {
    FXL_LOG(ERROR) << "M: Malformed packet: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  uintptr_t addr;
  size_t length;
  if (!fxl::StringToNumberWithError<uintptr_t>(params[0], &addr,
                                               fxl::Base::k16) ||
      !fxl::StringToNumberWithError<size_t>(params[1], &length,
                                            fxl::Base::k16)) {
    FXL_LOG(ERROR) << "M: Malformed params: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  FXL_VLOG(1) << fxl::StringPrintf("M: addr=0x%" PRIxPTR ", len=%lu", addr,
                                   length);

  auto data_bytes = DecodeByteArrayString(data);
  if (data_bytes.size() != length) {
    FXL_LOG(ERROR) << "M: payload length doesn't match length argument - "
                   << "payload size: " << data_bytes.size()
                   << ", length requested: " << length;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  // Short-circuit if |length| is 0.
  if (length &&
      !current_process->WriteMemory(addr, data_bytes.data(), length)) {
    FXL_LOG(ERROR) << "M: Failed to write memory";

    // TODO(armansito): The error code definitions from GDB aren't really
    // granular enough to aid debug various error conditions (e.g. we may want
    // to report why the memory write failed based on the zx_status_t returned
    // from Zircon). (See TODO in util.h).
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_q(const fxl::StringView& prefix,
                              const fxl::StringView& params,
                              ResponseCallback callback) {
  if (prefix == kAttached)
    return HandleQueryAttached(params, std::move(callback));

  if (prefix == kCurrentThreadId)
    return HandleQueryCurrentThreadId(params, std::move(callback));

  if (prefix == kFirstThreadInfo)
    return HandleQueryThreadInfo(true, std::move(callback));

  // The qRcmd packet is different than most. It uses , as a delimiter, not :.
  if (StartsWith(prefix, kRcmd))
    return HandleQueryRcmd(prefix.substr(std::strlen(kRcmd)),
                           std::move(callback));

  if (prefix == kSubsequentThreadInfo)
    return HandleQueryThreadInfo(false, std::move(callback));

  if (prefix == kSupported)
    return HandleQuerySupported(params, std::move(callback));

  if (prefix == kXfer)
    return HandleQueryXfer(params, std::move(callback));

  // TODO(dje): TO-195
  // - QDisableRandomization:VALUE ?
  // - qGetTLSAddr:THREAD-ID,OFFSET,LM
  // - qThreadExtraInfo,THREAD-ID ?

  return false;
}

bool CommandHandler::Handle_Q(const fxl::StringView& prefix,
                              const fxl::StringView& params,
                              ResponseCallback callback) {
  if (prefix == kNonStop)
    return HandleSetNonStop(params, std::move(callback));

  return false;
}

bool CommandHandler::Handle_T(const fxl::StringView& packet,
                              ResponseCallback callback) {
  // If there is no current process or if the current process isn't attached,
  // then report an error.
  Process* current_process = server_->current_process();
  if (!current_process || !current_process->IsAttached()) {
    FXL_LOG(ERROR) << "T: No inferior";
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  zx_koid_t tid;
  if (!fxl::StringToNumberWithError<zx_koid_t>(packet, &tid, fxl::Base::k16)) {
    FXL_LOG(ERROR) << "T: Malformed thread id given: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  Thread* thread = current_process->FindThreadById(tid);
  if (!thread) {
    FXL_LOG(ERROR) << "T: no such thread: " << packet;
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }
  if (!thread->IsLive()) {
    FXL_LOG(ERROR) << "T: thread found, but not live: " << packet;
    return ReplyWithError(ErrorCode::NOENT, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_v(const fxl::StringView& packet,
                              ResponseCallback callback) {
  if (StartsWith(packet, kAttach))
    return Handle_vAttach(packet.substr(std::strlen(kAttach)),
                          std::move(callback));
  if (StartsWith(packet, kCont))
    return Handle_vCont(packet.substr(std::strlen(kCont)), std::move(callback));
  if (StartsWith(packet, kKill))
    return Handle_vKill(packet.substr(std::strlen(kKill)), std::move(callback));
  if (StartsWith(packet, kRun))
    return Handle_vRun(packet.substr(std::strlen(kRun)), std::move(callback));

  return false;
}

bool CommandHandler::Handle_zZ(bool insert, const fxl::StringView& packet,
                               ResponseCallback callback) {
// Z0 needs more work. Disabled until ready.
// One issue is we need to support the swbreak feature.
#if 0
  // A Z packet contains the "type,addr,kind" parameters before all other
  // optional parameters, which follow an optional ';' character. Check to see
  // if there are any optional parameters:
  size_t semicolon = packet.find(';');

  // fxl::StringView::find returns npos if it can't find the character. Adjust
  // |semicolon| to point just beyond the end of |packet| so that
  // packet.substr() works..
  if (semicolon == fxl::StringView::npos)
    semicolon = packet.size();

  auto params = fxl::SplitString(packet.substr(0, semicolon), ",",
                                 fxl::kKeepWhitespace, fxl::kSplitWantNonEmpty);
  if (params.size() != 3) {
    FXL_LOG(ERROR) << "zZ: 3 required parameters missing";
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  size_t type;
  uintptr_t addr;
  size_t kind;
  if (!fxl::StringToNumberWithError<uintptr_t>(params[0], &type,
                                               fxl::Base::k16) ||
      !fxl::StringToNumberWithError<uintptr_t>(params[1], &addr,
                                               fxl::Base::k16) ||
      !fxl::StringToNumberWithError<size_t>(params[2], &kind, fxl::Base::k16)) {
    FXL_LOG(ERROR) << "zZ: Failed to parse |type|, |addr| and |kind|";
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  auto optional_params = packet.substr(semicolon);

  // "Remove breakpoint" packets don't contain any optional fields.
  if (!insert && !optional_params.empty()) {
    FXL_LOG(ERROR) << "zZ: Malformed packet";
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  switch (type) {
    case 0:
      if (insert)
        return InsertSoftwareBreakpoint(addr, kind, optional_params, std::move(callback));
      return RemoveSoftwareBreakpoint(addr, kind, std::move(callback));
    default:
      break;
  }

  FXL_LOG(WARNING) << "Breakpoints of type " << type
                   << " currently not supported";
#endif
  return false;
}

bool CommandHandler::HandleQueryAttached(const fxl::StringView& params,
                                         ResponseCallback callback) {
  // We don't support multiprocessing yet, so make sure we received the version
  // of qAttached that doesn't have a "pid" parameter.
  if (!params.empty())
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));

  // The response is "1" if we attached to an existing process, or "0" if we
  // created a new one. We currently don't support the former, so always send
  // "0".
  callback("0");
  return true;
}

bool CommandHandler::HandleQueryCurrentThreadId(const fxl::StringView& params,
                                                ResponseCallback callback) {
  // The "qC" packet has no parameters.
  if (!params.empty())
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));

  Thread* current_thread = server_->current_thread();
  if (!current_thread) {
    // If there is a current process and it has been started, pick one thread
    // and set that as the current one. This is our work around for lying to GDB
    // about setting a current thread in response to an early Hg0 packet.
    Process* current_process = server_->current_process();
    if (!current_process || !current_process->IsLive()) {
      FXL_LOG(ERROR) << "qC: Current thread has not been set";
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }

    FXL_VLOG(1) << "qC: Picking one arbitrary thread";
    current_thread = current_process->PickOneThread();
    if (!current_thread) {
      FXL_VLOG(1) << "qC: Failed to pick a thread";
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }
  }

  std::string thread_id =
      fxl::NumberToString<zx_koid_t>(current_thread->id(), fxl::Base::k16);

  std::string reply = "QC" + thread_id;
  callback(reply);
  return true;
}

bool CommandHandler::HandleQueryRcmd(const fxl::StringView& command,
                                     ResponseCallback callback) {
  auto cmd_string = DecodeString(command);
  std::vector<fxl::StringView> argv = fxl::SplitString(
      cmd_string, " ", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  if (argv.size() == 0) {
    // No command, just reply OK.
    return ReplyOK(std::move(callback));
  }
  auto cmd = argv[0];

  // We support both because qemu uses "quit" and GNU gdbserver uses "exit".
  if (cmd == kQuit || cmd == kExit) {
    if (argv.size() != 1)
      goto bad_command;
    ReplyOK(std::move(callback));
    server_->PostQuitMessageLoop(true);
  } else if (cmd == kHelp) {
    if (argv.size() != 1)
      goto bad_command;
    static constexpr char kHelpText[] =
        "help - print this help text\n"
        "exit - quit debugserver\n"
        "quit - quit debugserver\n"
        "set <parameter> <value>\n"
        "show <parameter>\n"
        "\n"
        "Parameters:\n"
        "  verbosity - useful range is -2 to 3 (-2 is most verbose)\n";
    callback(EncodeString(kHelpText));
  } else if (cmd == kSet) {
    if (argv.size() != 3)
      goto bad_command;
    if (!server_->SetParameter(argv[1], argv[2]))
      goto bad_command;
    ReplyOK(std::move(callback));
  } else if (cmd == kShow) {
    if (argv.size() != 2)
      goto bad_command;
    std::string value;
    if (!server_->GetParameter(argv[1], &value))
      goto bad_command;
    callback(EncodeString("Value is " + value + "\n"));
  } else {
    callback(EncodeString("Invalid monitor command\n"));
  }

  return true;

bad_command:
  // Errors are not reported via the usual mechanism. For rCmd, the usual
  // mechanism is for things like protocol errors. Instead we just want to
  // return the desired error message.
  callback(EncodeString("Invalid command\n"));
  return true;
}

bool CommandHandler::HandleQuerySupported(const fxl::StringView& params,
                                          ResponseCallback callback) {
  // We ignore the parameters for qSupported. Respond with the supported
  // features.
  callback(kSupportedFeatures);
  return true;
}

bool CommandHandler::HandleSetNonStop(const fxl::StringView& params,
                                      ResponseCallback callback) {
  // The only values we accept are "1" and "0".
  if (params.size() != 1)
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));

  // We currently only support non-stop mode.
  char value = params[0];
  if (value == '1')
    return ReplyOK(std::move(callback));

  if (value == '0')
    return ReplyWithError(ErrorCode::PERM, std::move(callback));

  FXL_LOG(ERROR) << "QNonStop received with invalid value: " << (unsigned)value;
  return ReplyWithError(ErrorCode::INVAL, std::move(callback));
}

bool CommandHandler::HandleQueryThreadInfo(bool is_first,
                                           ResponseCallback callback) {
  FXL_DCHECK(server_);

  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "Current process is not set";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
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
      FXL_LOG(ERROR) << "qsThreadInfo received without first receiving "
                     << "qfThreadInfo";
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    }

    in_thread_info_sequence_ = false;
    callback("l");
    return true;
  }

  // This is the first query. Check the sequence state for sanity.
  if (in_thread_info_sequence_) {
    FXL_LOG(ERROR) << "qfThreadInfo received while already in an active "
                   << "sequence";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  current_process->EnsureThreadMapFresh();

  std::deque<std::string> thread_ids;
  size_t buf_size = 0;
  current_process->ForEachLiveThread([&thread_ids, &buf_size](Thread* thread) {
    std::string thread_id =
        fxl::NumberToString<zx_koid_t>(thread->id(), fxl::Base::k16);
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
  JoinStrings(thread_ids, ',', buffer.get() + 1, buf_size - 1);

  callback(fxl::StringView(buffer.get(), buf_size));

  return true;
}

bool CommandHandler::HandleQueryXfer(const fxl::StringView& params,
                                     ResponseCallback callback) {
  // We only support qXfer:auxv:read::
  // TODO(dje): TO-195
  // - qXfer::osdata::read::OFFSET,LENGTH
  // - qXfer:memory-map:read::OFFSET,LENGTH ?
  // - qXfer:libraries-svr4:read:ANNEX:OFFSET,LENGTH ?
  // - qXfer:features:read:ANNEX:OFFSET,LENGTH ?
  fxl::StringView auxv_read("auxv:read::");
  if (!StartsWith(params, auxv_read))
    return false;

  // Parse offset,length
  auto args = fxl::SplitString(params.substr(auxv_read.size()), ",",
                               fxl::kKeepWhitespace, fxl::kSplitWantNonEmpty);
  if (args.size() != 2) {
    FXL_LOG(ERROR) << "qXfer:auxv:read:: Malformed params: " << params;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  size_t offset, length;
  if (!fxl::StringToNumberWithError<size_t>(args[0], &offset, fxl::Base::k16) ||
      !fxl::StringToNumberWithError<size_t>(args[1], &length, fxl::Base::k16)) {
    FXL_LOG(ERROR) << "qXfer:auxv:read:: Malformed params: " << params;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "qXfer:auxv:read: No current process is not set";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // Build the auxiliary vector. This definition is provided by the Linux manual
  // page for the proc pseudo-filesystem (i.e. 'man proc'):
  // "This contains the contents of the ELF interpreter information passed to
  // the process at exec time. The format is one unsigned long ID plus one
  // unsigned long value for each entry. The last entry contains two zeros."
  // On Fuchsia we borrow this concept to save inventing something new.
  // We may have to eventually, but this works for now.
  // There is an extra complication that all the needed values aren't available
  // when the process starts: e.g., AT_ENTRY - the executable isn't loaded
  // until sometime after the process starts.
  constexpr size_t kMaxAuxvEntries = 10;
  struct {
    unsigned long key;
    unsigned long value;
  } auxv[kMaxAuxvEntries];

#define ADD_AUXV(_key, _value) \
  do {                         \
    auxv[n].key = (_key);      \
    auxv[n].value = (_value);  \
    ++n;                       \
  } while (0)

  size_t n = 0;
  ADD_AUXV(AT_BASE, current_process->base_address());
  if (current_process->DsosLoaded()) {
    const dsoinfo_t* exec = current_process->GetExecDso();
    if (exec) {
      ADD_AUXV(AT_ENTRY, exec->entry);
      ADD_AUXV(AT_PHDR, exec->phdr);
      ADD_AUXV(AT_PHENT, exec->phentsize);
      ADD_AUXV(AT_PHNUM, exec->phnum);
    }
  }
  ADD_AUXV(AT_NULL, 0);
  FXL_DCHECK(n <= countof(auxv));

#undef ADD_AUXV

  // We allow setting sizeof(auxv) as the offset, which would effectively result
  // in reading 0 bytes.
  if (offset > sizeof(auxv)) {
    FXL_LOG(ERROR) << "qXfer:auxv:read: invalid offset";
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  size_t end = n * sizeof(auxv[0]);
  size_t rsp_len = std::min(end - offset, length);
  char rsp[1 + rsp_len];

  rsp[0] = 'l';
  memcpy(rsp + 1, auxv + offset, rsp_len);

  callback(fxl::StringView(rsp, sizeof(rsp)));
  return true;
}

bool CommandHandler::Handle_vAttach(const fxl::StringView& packet,
                                    ResponseCallback callback) {
  // TODO(dje): The terminology we use makes this confusing.
  // Here when you see "process" think "inferior". An inferior must be created
  // first, and then we can attach the inferior to a process.
  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "vAttach: no inferior selected";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  zx_koid_t pid;
  if (!fxl::StringToNumberWithError<zx_koid_t>(packet, &pid, fxl::Base::k16)) {
    FXL_LOG(ERROR) << "vAttach:: Malformed pid: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  switch (current_process->state()) {
    case Process::State::kNew:
    case Process::State::kGone:
      break;
    default:
      FXL_LOG(ERROR)
          << "vAttach: need to kill the currently running process first";
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  if (!current_process->Attach(pid)) {
    FXL_LOG(ERROR) << "vAttach: failed to attach to inferior " << pid;
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // It's Attach()'s job to mark the process as live, since it knows we just
  // attached to an already running program.
  FXL_DCHECK(current_process->IsLive());

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_vCont(const fxl::StringView& packet,
                                  ResponseCallback callback) {
  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "vCont: no current process to run!";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  ThreadActionList actions(packet, current_process->id());
  if (!actions.valid()) {
    FXL_LOG(ERROR) << "vCont: \"" << packet << "\": error / not supported.";
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  FXL_DCHECK(current_process->IsLive());
  FXL_DCHECK(current_process->IsAttached());

  // Before we start calling GetAction we need to resolve "pick one" thread
  // values.
  for (auto e : actions.actions()) {
    if (e.tid() == 0) {
      FXL_DCHECK(e.pid() > 0);
      // TODO(dje): For now we assume there is only one process.
      FXL_DCHECK(current_process->id() == e.pid() ||
                 e.pid() == ThreadActionList::kAll);
      Thread* t = current_process->PickOneThread();
      if (t)
        e.set_picked_tid(t->id());
    }
  }
  actions.MarkPickOnesResolved();

  // First pass over all actions: Find any errors that we can so that we
  // don't cause any thread to run if there's an error.

  bool action_list_ok = true;
  current_process->ForEachLiveThread(
      [&actions, ok_ptr = &action_list_ok](Thread* thread) {
        zx_koid_t pid = thread->process()->id();
        zx_koid_t tid = thread->id();
        ThreadActionList::Action action = actions.GetAction(pid, tid);
        switch (action) {
          case ThreadActionList::Action::kStep:
            switch (thread->state()) {
              case Thread::State::kNew:
                FXL_LOG(ERROR) << "vCont;s: can't step thread in kNew state";
                *ok_ptr = false;
                return;
              default:
                break;
            }
          default:
            break;
        }
      });
  if (!action_list_ok)
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));

  current_process->ForEachLiveThread([&actions](Thread* thread) {
    zx_koid_t pid = thread->process()->id();
    zx_koid_t tid = thread->id();
    ThreadActionList::Action action = actions.GetAction(pid, tid);
    FXL_VLOG(1) << "vCont; Thread " << thread->GetDebugName()
                << " state: " << thread->StateName(thread->state())
                << " action: " << ThreadActionList::ActionToString(action);
    switch (action) {
      case ThreadActionList::Action::kContinue:
        switch (thread->state()) {
          case Thread::State::kNew:
          case Thread::State::kStopped:
            thread->Resume();
            break;
          default:
            break;
        }
      case ThreadActionList::Action::kStep:
        switch (thread->state()) {
          case Thread::State::kStopped:
            thread->Step();
            break;
          default:
            break;
        }
      default:
        break;
    }
  });

  // We defer sending a stop-reply packet. Server will send it out when threads
  // stop. At this point in time GDB is just expecting "OK".
  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_vKill(const fxl::StringView& packet,
                                  ResponseCallback callback) {
  FXL_VLOG(2) << "Handle_vKill: " << packet;

  Process* current_process = server_->current_process();
  if (!current_process) {
    // This can't happen today, but it might eventually.
    FXL_LOG(ERROR) << "vRun: no current process to kill!";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  zx_koid_t pid;
  if (!fxl::StringToNumberWithError<zx_koid_t>(packet, &pid, fxl::Base::k16)) {
    FXL_LOG(ERROR) << "vAttach:: Malformed pid: " << packet;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  // Since we only support one process at the moment, only allow killing
  // that one.
  if (pid != current_process->id()) {
    FXL_LOG(ERROR) << "vAttach:: not our pid: " << pid;
    return ReplyWithError(ErrorCode::INVAL, std::move(callback));
  }

  switch (current_process->state()) {
    case Process::State::kNew:
    case Process::State::kGone:
      FXL_LOG(ERROR) << "vKill: process not running";
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
    default:
      break;
  }

  if (!current_process->Kill()) {
    FXL_LOG(ERROR) << "Failed to kill inferior";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

bool CommandHandler::Handle_vRun(const fxl::StringView& packet,
                                 ResponseCallback callback) {
  FXL_VLOG(2) << "Handle_vRun: " << packet;

  Process* current_process = server_->current_process();
  if (!current_process) {
    // This can't happen today, but it might eventually.
    FXL_LOG(ERROR) << "vRun: no current process to run!";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  if (!packet.empty()) {
    std::vector<std::string> argv = BuildArgvFor_vRun(packet);
    current_process->set_argv(argv);
  }

  switch (current_process->state()) {
    case Process::State::kNew:
    case Process::State::kGone:
      break;
    default:
      FXL_LOG(ERROR)
          << "vRun: need to kill the currently running process first";
      return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  if (!current_process->Initialize()) {
    FXL_LOG(ERROR) << "Failed to set up inferior";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // On Linux, the program is considered "live" after vRun, e.g. $pc is set. On
  // Zircon, calling zx_process_start (called by Process::Start()) creates a
  // synthetic exception of type ZX_EXCP_START if a debugger is attached to the
  // process and halts until a call to zx_task_resume (i.e. called by
  // Thread::Resume() in gdbserver).
  if (!current_process->Start()) {
    FXL_LOG(ERROR) << "vRun: Failed to start process";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  FXL_DCHECK(current_process->IsLive());

  // We defer sending a stop-reply packet. Server will send it out when it
  // receives an OnThreadStarting() event from |current_process|.

  return true;
}

bool CommandHandler::InsertSoftwareBreakpoint(
    uintptr_t addr, size_t kind, const fxl::StringView& optional_params,
    ResponseCallback callback) {
  FXL_VLOG(1) << fxl::StringPrintf(
      "Insert software breakpoint at %" PRIxPTR ", kind: %lu", addr, kind);

  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "No current process exists";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  // TODO(armansito): Handle |optional_params|.

  if (!current_process->breakpoints()->InsertSoftwareBreakpoint(addr, kind)) {
    FXL_LOG(ERROR) << "Failed to insert software breakpoint";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

bool CommandHandler::RemoveSoftwareBreakpoint(uintptr_t addr, size_t kind,
                                              ResponseCallback callback) {
  FXL_VLOG(1) << fxl::StringPrintf("Remove software breakpoint at %" PRIxPTR,
                                   addr);

  Process* current_process = server_->current_process();
  if (!current_process) {
    FXL_LOG(ERROR) << "No current process exists";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  if (!current_process->breakpoints()->RemoveSoftwareBreakpoint(addr)) {
    FXL_LOG(ERROR) << "Failed to remove software breakpoint";
    return ReplyWithError(ErrorCode::PERM, std::move(callback));
  }

  return ReplyOK(std::move(callback));
}

}  // namespace debugserver
