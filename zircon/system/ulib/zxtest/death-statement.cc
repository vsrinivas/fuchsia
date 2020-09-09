// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/exception.h>
#include <lib/zx/port.h>
#include <lib/zx/task.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/syscalls/port.h>
#include <zircon/types.h>

#include <cstdint>
#include <string>

#include <fbl/auto_call.h>
#include <zxtest/base/death-statement.h>

namespace zxtest {
namespace internal {
namespace {

#define STRING(x) #x
#define MAKE_MESSAGE(reason, line) \
  "Death Test Internal Error at " __FILE__ ":" STRING(line) " " reason

#define SET_ERROR(var, msg)            \
  do {                                 \
    var = MAKE_MESSAGE(msg, __LINE__); \
  } while (0)

// Keys used to filter exception ports.
enum class PortKeys : uint64_t {
  // Exception raised and handled.
  kException = 1,
  kThreadTermination = 2,
  kThreadCompletion = 3,
  kThreadError = 4,
};

// It is only safe to transmit within the same process.
struct ErrorInfo {
  zx_port_packet_t ToPacket() {
    zx_port_packet_t packet;
    packet.key = static_cast<uint64_t>(PortKeys::kThreadError);
    packet.type = ZX_PKT_TYPE_USER;
    packet.user.u64[0] = reinterpret_cast<uint64_t>(this);
    return packet;
  }

  std::string error_msg;
};

struct RoutineArgs {
  // Statement to be executed.
  fit::function<void()> statement;

  // Port for signaling thread termination. This is used to unblock the main thread.
  zx::port event_port;

  // The thread will bind this channel as the exception handler.
  zx::channel exception_channel;
};

void SendError(const zx::port& port, const char* message) {
  std::unique_ptr<ErrorInfo> info = std::make_unique<ErrorInfo>();
  info->error_msg = message;
  zx_port_packet_t packet = info->ToPacket();
  zx_status_t result = port.queue(&packet);
  if (result != ZX_OK) {
    fprintf(stderr, "%s.\nDeath Test Fatal Error: zx::port::queue failed with status %s.\n",
            info->error_msg.c_str(), zx_status_get_string(result));
    fflush(stderr);
    exit(-1);
  }
  // This will we released by the main thread once the error message is dequeued.
  info.release();
  return;
}

// Try to exit cleanly, if not just kill the entire process.
#define SEND_ERROR_AND_RETURN(port, message) \
  do {                                       \
    const char* error_message;               \
    SET_ERROR(error_message, message);       \
    SendError(port, error_message);          \
    return -1;                               \
  } while (0)

// Even though it is a separate thread, it is stalling the main thread, until it completes,
// which is why it is safe to interact with the test harness.
int RoutineThread(void* args) {
  RoutineArgs* routine_args = reinterpret_cast<RoutineArgs*>(args);
  zx::unowned_thread thread = zx::thread::self();

  auto signal_completion = fbl::MakeAutoCall([&routine_args]() {
    zx_port_packet_t packet;
    packet.type = ZX_PKT_TYPE_USER;
    packet.key = static_cast<uint64_t>(PortKeys::kThreadCompletion);
    if (routine_args->event_port.queue(&packet) != ZX_OK) {
      fprintf(stderr, "Death Test Fatal Error: zx::port::queue failed.\n");
      fflush(stderr);
      exit(-1);
    }
  });

  // Register thread termination with the port.
  if (thread->wait_async(routine_args->event_port,
                         static_cast<uint64_t>(PortKeys::kThreadTermination), ZX_THREAD_TERMINATED,
                         0) != ZX_OK) {
    SEND_ERROR_AND_RETURN(routine_args->event_port, "Failed to register thread events with port");
  }

  if (!routine_args->statement) {
    SEND_ERROR_AND_RETURN(routine_args->event_port, "Empty death statement");
  }

  // Bind the exception channel, so main thread can inspect for exceptions once this thread is
  // terminated.
  if (thread->create_exception_channel(0, &routine_args->exception_channel)) {
    SEND_ERROR_AND_RETURN(routine_args->event_port, "Failed to create exception_channel");
  }

  // Register the exception channel with the port so we can process exceptions and
  // unblock/terminate this thread.
  if (routine_args->exception_channel.wait_async(routine_args->event_port,
                                                 static_cast<uint64_t>(PortKeys::kException),
                                                 ZX_CHANNEL_READABLE, 0) != ZX_OK) {
    SEND_ERROR_AND_RETURN(routine_args->event_port,
                          "Failed to register exception channel with port");
  }
  routine_args->statement();

  return 0;
}

__NO_SAFESTACK static void thrd_exit_success() { thrd_exit(0); }

// Extracts the thread from |exception| and causes it to exit.
zx_status_t ExitExceptionThread(zx::exception exception, std::string* error_message) {
  zx::thread thread;
  zx_status_t status = exception.get_thread(&thread);
  if (status != ZX_OK) {
    SET_ERROR(*error_message, "Failed to obtain thread from exception handle");
    return status;
  }

  if (!thread.is_valid()) {
    SET_ERROR(*error_message, "Exception contained invalid exception handle");
    return ZX_ERR_INTERNAL;
  }

  // Set the thread's registers to `zx_thread_exit`.
  zx_thread_state_general_regs_t regs;
  status = thread.read_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    SET_ERROR(*error_message, "Failed to read exception thread state");
    return status;
  }

#if defined(__aarch64__)
  regs.pc = reinterpret_cast<uintptr_t>(thrd_exit_success);
#elif defined(__x86_64__)
  regs.rip = reinterpret_cast<uintptr_t>(thrd_exit_success);
#else
#error "what machine?"
#endif

  status = thread.write_state(ZX_THREAD_STATE_GENERAL_REGS, &regs, sizeof(regs));
  if (status != ZX_OK) {
    SET_ERROR(*error_message, "Failed to write exception thread state");
    return status;
  }

  // Clear the exception so the thread continues.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  status = exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
  if (status != ZX_OK) {
    SET_ERROR(*error_message, "Failed to handle exception");
    return status;
  }
  exception.reset();

  // Wait until the thread exits.
  status = thread.wait_one(ZX_THREAD_TERMINATED, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    SET_ERROR(*error_message, "Failed to wait for thread exit");
    return status;
  }
  return ZX_OK;
}

}  // namespace

DeathStatement::DeathStatement(fit::function<void()> statement) : statement_(std::move(statement)) {
  state_ = State::kUnknown;
  error_message_ = "";
}

void DeathStatement::Execute() {
  RoutineArgs routine_args;
  routine_args.statement = std::move(statement_);
  state_ = State::kStarted;

  if (zx::port::create(0u, &routine_args.event_port) != ZX_OK) {
    SET_ERROR(error_message_, "Failed to created event_port");
    return;
  }

  thrd_t death_thread;
  if (thrd_create(&death_thread, &RoutineThread, &routine_args) != thrd_success) {
    SET_ERROR(error_message_, "Failed to create death_thred");
    return;
  }
  Listen(routine_args.event_port, routine_args.exception_channel);
  thrd_join(death_thread, nullptr);
}

// Listens for events on |event_port|. Eventually the thread will register its termination and the
// exception channel so that they can be processed.
void DeathStatement::Listen(const zx::port& event_port, const zx::channel& exception_channel) {
  zx_port_packet_t packet;
  // Wait until either the port is closed or a packet arrives.
  while (event_port.wait(zx::time::infinite(), &packet) == ZX_OK) {
    switch (static_cast<PortKeys>(packet.key)) {
      case PortKeys::kException:
        if (HandleException(exception_channel)) {
          return;
        }
        break;
      case PortKeys::kThreadCompletion:
      case PortKeys::kThreadTermination:
        // We only mark the execution as success if there was no internal error.
        if (state_ == State::kStarted) {
          state_ = DeathStatement::State::kSuccess;
        }
        return;
      case PortKeys::kThreadError: {
        ErrorInfo* info = reinterpret_cast<ErrorInfo*>(packet.user.u64[0]);
        state_ = State::kInternalError;
        error_message_ = std::move(info->error_msg);
        delete info;
      } break;
      default:
        continue;
    }
  }

  // If this is reached, we are in a bad state.
  state_ = State::kBadState;
  return;
}

bool DeathStatement::HandleException(const zx::channel& exception_channel) {
  zx_exception_info_t exception_info;
  zx::exception exception;
  uint32_t num_handles = 1;
  uint32_t num_bytes = sizeof(zx_exception_info_t);

  auto set_internal_error = fbl::MakeAutoCall([this]() { state_ = State::kInternalError; });

  if (exception_channel.read(0, &exception_info, exception.reset_and_get_address(),
                             sizeof(zx_exception_info_t), 1, &num_bytes, &num_handles) != ZX_OK) {
    SET_ERROR(error_message_, "Failed to read exception from exception channel");
    return true;
  }

  if (num_handles != 1 || num_bytes != sizeof(zx_exception_info_t)) {
    SET_ERROR(error_message_, "Missing exception handle or exception info");
    return true;
  }

  if (!exception.is_valid()) {
    SET_ERROR(error_message_, "Exception handle is not valid");
    return true;
  }

  set_internal_error.cancel();
  // Ignore exceptions that are not really crashes and resume the thread.
  switch (exception_info.type) {
    case ZX_EXCP_THREAD_STARTING:
    case ZX_EXCP_THREAD_EXITING:
      // Returning will close the exception handle and will resume the blocked threads.
      return false;
    default:
      break;
  }

  // If we fail to kill the thread, we set the statement to a bad state so the harness can exit
  // cleanly.
  if (ExitExceptionThread(std::move(exception), &error_message_) != ZX_OK) {
    // ExitExceptionThread sets error_message_ correctly.
    state_ = DeathStatement::State::kBadState;
    return true;
  }

  // If everything went ok, we mark the statement as completed with exception.
  state_ = DeathStatement::State::kException;
  return true;
}

}  // namespace internal
}  // namespace zxtest
