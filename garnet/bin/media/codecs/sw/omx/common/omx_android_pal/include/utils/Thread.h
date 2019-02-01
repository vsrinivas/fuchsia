// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_THREAD_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_THREAD_H_

#include <utils/Condition.h>
#include <utils/Errors.h>
#include <utils/RefBase.h>
#include <utils/Timers.h>

#include <thread>

extern "C" {

enum {
  ANDROID_PRIORITY_FOREGROUND = -2,
};
}

namespace android {

enum {
  PRIORITY_DEFAULT = 0,
};

// DO NOT USE FOR NEW CODE - please use std::thread, or something else.  This
// shim is only to allow some android code to compile and run on Fuchsia.

// Intentionally do not support repeated calls to run(), even if the android
// implementation may be trying to support that (unclear).  In this
// implementation an instance of this class can only correspond to up to one
// underlying thread lifetime, by design.
//
// The proper way to wait until the thread is really actually fully done running
// is to call requestExitAndWait(), or requestExit() and ~Thread.  FWIW, until
// that's done, it's not safe to do something that could change running code
// such as un-load of a shared library that contains an instance of the code of
// this class, since the tail end of _threadLoop() could still be running on the
// thread.  We expect libc++.so to remain loaded, so we don't need to analyze
// whether std::thread code itself is robust to code unloading.  We don't
// currently expect to un-load any code (including the code of this class), but
// this class should be reasonably ready for code unloading should it be added
// at some point.

// The virtual inheritance is probably not currently important for the current
// Fuchsia usage, but is here to maximize compatibility for now.
class Thread : virtual public RefBase {
 public:
  // This Thread shim on Fuchsia only supports can_call_java == false, else
  // abort().
  explicit Thread(bool can_call_java = true);
  virtual ~Thread();
  virtual status_t run(const char* thread_name,
                       int32_t thread_priority = PRIORITY_DEFAULT,
                       size_t stack_size = 0);
  virtual void requestExit();
  status_t requestExitAndWait();

 protected:
  // This would be private or completely removed in the Fuchsia implementation
  // except for ALooper using it to stash the thread ID.
  virtual status_t readyToRun();

 private:
  virtual bool threadLoop() = 0;
  void _threadLoop();
  bool isExitRequested() const;
  void joinCommon();
  mutable std::mutex lock_;
  bool is_run_called_ = false;
  std::unique_ptr<std::thread> thread_;
  // The status of starting the thread, not anything more.
  status_t start_status_ = NO_ERROR;
  bool is_exit_requested_ = false;
  bool is_joiner_selected_ = false;
  bool is_joined_ = false;
  std::condition_variable joined_condition_;
  sp<Thread> hold_self_;
};

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_UTILS_THREAD_H_
