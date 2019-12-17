// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/spawn.h>
#include <lib/zx/channel.h>
#include <lib/zx/clock.h>
#include <lib/zx/process.h>
#include <zircon/processargs.h>
#include <zircon/utc.h>

#include <string>

#include <fbl/auto_call.h>
#include <zxtest/zxtest.h>

namespace {

const std::string kHelperFlag{"utc-procargs-helper"};
constexpr zx::duration kProcessTerminateTimeout = zx::sec(20);

// A small wrapper used to launch a process which will fetch the current clock
// from the environment which should have been set up by libc, and send back
// to us details about the clock that it sees.
class TargetProcess {
 public:
  struct ResponseMessage {
    // Note: this is not an actual handle.  It is simply the value observed by
    // the process target.  We use it to make sure that the handle is invalid
    // when it should be.
    zx_handle_t observed_utc_handle;
    zx_koid_t observed_utc_koid;
    zx_rights_t observed_utc_rights;
  };

  static void SetProgramName(const char* program_name) { program_name_ = program_name; }
  static int Main();

  TargetProcess() = default;

  // No copy or move, either via construction or assignment.
  TargetProcess(const TargetProcess&) = delete;
  TargetProcess(TargetProcess&&) = delete;
  TargetProcess& operator=(const TargetProcess&) = delete;
  TargetProcess& operator=(TargetProcess&&) = delete;

  ~TargetProcess() { Stop(); }

  void Run(zx::clock clock_to_send);
  const zx::channel& control_channel() const { return control_channel_; }

 private:
  static const char* program_name_;

  void Stop() {
    // If the target process is valid, attempt to kill it.  It should have
    // exited already, but something must have gone terribly wrong.
    if (target_process_) {
      target_process_.kill();
      target_process_.reset();
    }
    control_channel_.reset();
  }

  zx::process target_process_;
  zx::channel control_channel_;
};

const char* TargetProcess::program_name_ = nullptr;

// Run the target process, passing the clock provided (if any) and wait for it to exit.
void TargetProcess::Run(zx::clock clock_to_send) {
  auto on_failure = fbl::MakeAutoCall([this]() { Stop(); });

  // Make sure that we have a program name and have not already started.
  ASSERT_NOT_NULL(program_name_);
  ASSERT_EQ(target_process_.get(), ZX_HANDLE_INVALID);
  ASSERT_EQ(control_channel_.get(), ZX_HANDLE_INVALID);

  // Create the channel we will use for talking to our external process.
  zx::channel remote;
  ASSERT_OK(zx::channel::create(0, &control_channel_, &remote));

  const char* args[] = {program_name_, kHelperFlag.c_str(), nullptr};
  size_t handles_to_send = clock_to_send.is_valid() ? 2 : 1;
  struct fdio_spawn_action startup_handles[] = {
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
       .h = {.id = PA_HND(PA_USER0, 0), .handle = remote.release()}},
      {.action = FDIO_SPAWN_ACTION_ADD_HANDLE,
       .h = {.id = PA_HND(PA_CLOCK_UTC, 0), .handle = clock_to_send.release()}}};

  char err_msg_out[FDIO_SPAWN_ERR_MSG_MAX_LENGTH];
  zx_status_t res = fdio_spawn_etc(ZX_HANDLE_INVALID, FDIO_SPAWN_CLONE_ALL, program_name_, args,
                                   nullptr, handles_to_send, startup_handles,
                                   target_process_.reset_and_get_address(), err_msg_out);
  ASSERT_OK(res, "%s", err_msg_out);

  // Wait for the process we spawned to exit.  We wait a finite (but very long)
  // amount of time for this to happen in the hopes that if something goes wrong
  // that we will have a chance to kill the process we spawned instead of
  // needing to hope that our test framework will be able to do so for us.
  ASSERT_OK(target_process_.wait_one(ZX_PROCESS_TERMINATED,
                                     zx::deadline_after(kProcessTerminateTimeout), nullptr));

  // OK, the process exited.  Go ahead and close the handle so that we don't
  // bother to try and kill it later on.
  target_process_.reset();

  // Things went well!  Cancel our on_failure cleanup routine.
  on_failure.cancel();
}

int TargetProcess::Main() {
  // Get a hold of the channel we will use to respond to the test harness with,
  // extract the details of the clock object (the koid and the rights), and send
  // it back to the test harness for validation.  If anything goes wrong here,
  // return the non-zero line number at which failure occurred in an attempt to
  // give an indication to the test process something to log which might be
  // helpful for someone trying to figure out where the helper process failed in
  // the case that all they have to go on are some automated test logs.
  //
  // If things go well, return 0 to indicate success.
  zx::channel response_channel(zx_take_startup_handle(PA_HND(PA_USER0, 0)));
  if (!response_channel.is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Now take a peek at our clock handle as stashed by the runtime.
  TargetProcess::ResponseMessage response{};
  response.observed_utc_handle = zx_utc_reference_get();
  zx::unowned_clock utc_clock(response.observed_utc_handle);

  if (utc_clock->is_valid()) {
    zx_info_handle_basic_t clock_info{};
    zx_status_t res = utc_clock->get_info(ZX_INFO_HANDLE_BASIC, &clock_info, sizeof(clock_info),
                                          nullptr, nullptr);

    if (res != ZX_OK) {
      return res;
    }

    response.observed_utc_koid = clock_info.koid;
    response.observed_utc_rights = clock_info.rights;
  }

  // Send a message back with the details of the clock that the runtime has
  // stashed for us.
  return response_channel.write(0, &response, sizeof(response), nullptr, 0);
}

// We will end up running three variants of the test, but the vast majority of
// the code that we are going to run is common, so we pick which variant we want
// using an enum at runtime to reduce code duplication.  Note, if there was a
// reason to, this decision could be made using templates and expanded at
// compile time instead.
enum class TransferTestFlavor {
  kNoHandleProvided,
  kReadOnlyHandleProvided,
  kReadWriteHandleProvided,
};

void TransferTestCommon(TransferTestFlavor flavor) {
  zx::clock the_clock;
  zx_info_handle_basic_t clock_info{};

  // If this test involves actually creating a clock, create it now, start it,
  // reduce its rights to the appropriate level, and stash its basic information
  // for later validation.
  if ((flavor == TransferTestFlavor::kReadOnlyHandleProvided) ||
      (flavor == TransferTestFlavor::kReadWriteHandleProvided)) {
    // Just go with a default clock for now.  We don't really care all that much
    // about the features of the clock for these tests.
    ASSERT_OK(zx::clock::create(0, nullptr, &the_clock));

    // Start the clock, just in case the environment we are sending the clock to
    // has any opinions at all as to whether or not the clock should be running.
    ASSERT_OK(the_clock.update(zx::clock::update_args().set_value(zx::time(0))));

    // Query and stash the basic info
    ASSERT_OK(the_clock.get_info(ZX_INFO_HANDLE_BASIC, &clock_info, sizeof(clock_info), nullptr,
                                 nullptr));

    // If this test involves a read-only clock, reduce the rights on our handle.
    if (flavor == TransferTestFlavor::kReadOnlyHandleProvided) {
      clock_info.rights &= ~ZX_RIGHT_WRITE;
      ASSERT_OK(the_clock.replace(clock_info.rights, &the_clock));
    }
  }

  // Now go ahead and run, passing it the clock we created (if any)
  TargetProcess target_process;
  ASSERT_NO_FATAL_FAILURES(target_process.Run(std::move(the_clock)));

  // At this point, the process should have already sent us a response in the
  // control channel and exited.  Go ahead and read the response now.
  TargetProcess::ResponseMessage response;
  ASSERT_OK(target_process.control_channel().read(0, &response, nullptr, sizeof(response), 0,
                                                  nullptr, nullptr));

  // Now just check the results based on the type of test we are running.
  switch (flavor) {
    // If this was the no-handle test, then we should just have HANDLE_INVALID
    // for the handle value, and nothing else.
    case TransferTestFlavor::kNoHandleProvided:
      EXPECT_EQ(ZX_HANDLE_INVALID, response.observed_utc_handle);
      break;

    // For either the read-only, or the read write tests, the handle should not
    // be invalid, the koid/rights should match what we sent to the process
    // exactly.  We do not expect the runtime to reduce the rights any further.
    case TransferTestFlavor::kReadOnlyHandleProvided:
    case TransferTestFlavor::kReadWriteHandleProvided:
      EXPECT_NE(ZX_HANDLE_INVALID, response.observed_utc_handle);
      EXPECT_EQ(clock_info.koid, response.observed_utc_koid);
      EXPECT_EQ(clock_info.rights, response.observed_utc_rights);
      break;

    default:
      ASSERT_TRUE(false);
      break;
  };
}

TEST(UtcProcargsTestCase, TransferNoHandle) {
  ASSERT_NO_FAILURES(TransferTestCommon(TransferTestFlavor::kNoHandleProvided));
}

TEST(UtcProcargsTestCase, TransferReadOnly) {
  ASSERT_NO_FAILURES(TransferTestCommon(TransferTestFlavor::kReadOnlyHandleProvided));
}

TEST(UtcProcargsTestCase, TransferReadWrite) {
  ASSERT_NO_FAILURES(TransferTestCommon(TransferTestFlavor::kReadWriteHandleProvided));
}

}  // namespace

int main(int argc, char** argv) {
  TargetProcess::SetProgramName(argv[0]);

  // If we were the spawned helper process, then fork off to the helper process
  // behavior instead of running the tests.
  if ((argc == 2) && !strcmp(argv[1], kHelperFlag.c_str())) {
    return TargetProcess::Main();
  }

  return RUN_ALL_TESTS(argc, argv);
}
