// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/common/fidl_thread.h"

#include <lib/sync/cpp/completion.h>
#include <lib/zx/time.h>

#include <thread>

namespace media_audio {

// static
std::shared_ptr<FidlThread> FidlThread::CreateFromNewThread(std::string name) {
  auto loop = std::make_unique<async::Loop>(&kAsyncLoopConfigNeverAttachToThread);
  loop->StartThread(name.c_str());

  // async::Loop::StartThread reports the thread's `thrd_t` but not the `std::thread::id`.
  // To get the latter, post a task to the thread.
  std::optional<std::thread::id> thread_id;
  libsync::Completion done;

  async::PostTask(loop->dispatcher(), [&done, &thread_id]() {
    thread_id = std::this_thread::get_id();
    done.Signal();
  });

  FX_CHECK(done.Wait(zx::sec(5)) == ZX_OK)
      << "Deadlock in FidlThread::Create while creating thread '" << name << "'";
  FX_CHECK(thread_id.has_value());

  return Create(std::move(name), *thread_id, loop->dispatcher(), std::move(loop));
}

// static
std::shared_ptr<FidlThread> FidlThread::CreateFromCurrentThread(std::string name,
                                                                async_dispatcher_t* dispatcher) {
  return Create(std::move(name), std::this_thread::get_id(), dispatcher, nullptr);
}

// static
std::shared_ptr<FidlThread> FidlThread::Create(std::string name, std::thread::id thread_id,
                                               async_dispatcher_t* dispatcher,
                                               std::unique_ptr<async::Loop> loop) {
  struct WithPublicCtor : public FidlThread {
   public:
    WithPublicCtor(std::string name, std::thread::id thread_id, async_dispatcher_t* dispatcher,
                   std::unique_ptr<async::Loop> loop)
        : FidlThread(std::move(name), thread_id, dispatcher, std::move(loop)) {}
  };
  return std::make_shared<WithPublicCtor>(std::string(name), thread_id, dispatcher,
                                          std::move(loop));
}

}  // namespace media_audio
