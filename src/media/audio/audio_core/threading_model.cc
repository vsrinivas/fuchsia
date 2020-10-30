// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/threading_model.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <mutex>
#include <string>
#include <unordered_map>

#include "src/media/audio/audio_core/utils.h"

namespace media::audio {
namespace {

void SetMixDispatcherThreadProfile(async_dispatcher_t* dispatcher) {
  zx::profile profile;
  zx_status_t status = AcquireHighPriorityProfile(&profile);
  if (status != ZX_OK) {
    FX_LOGS(ERROR)
        << "Unable to acquire high priority profile; mix threads will run at normal priority";
    return;
  }
  FX_DCHECK(profile);
  async::PostTask(dispatcher, [profile = std::move(profile)] {
    zx_status_t status = zx::thread::self()->set_profile(profile, 0);
    FX_DCHECK(status == ZX_OK);
  });
}

struct ExecutionDomainHolder {
  ExecutionDomainHolder() : ExecutionDomainHolder(&kAsyncLoopConfigNoAttachToCurrentThread) {}
  ExecutionDomainHolder(const async_loop_config_t* loop_config)
      : loop{loop_config}, executor{loop.dispatcher()} {}
  async::Loop loop;
  async::Executor executor;
  ExecutionDomain domain{loop.dispatcher(), &executor};
};

class ThreadingModelBase : public ThreadingModel {
 public:
  ~ThreadingModelBase() override = default;

  // |ThreadingModel|
  ExecutionDomain& FidlDomain() final { return fidl_domain_.domain; }
  ExecutionDomain& IoDomain() final { return io_domain_.domain; }
  void RunAndJoinAllThreads() override {
    io_domain_.loop.StartThread("io");
    fidl_domain_.loop.Run();
    IoDomain().PostTask([this] { io_domain_.loop.Quit(); });
    io_domain_.loop.JoinThreads();
  }
  void Quit() final {
    FidlDomain().PostTask([this] { fidl_domain_.loop.Quit(); });
  }

 private:
  ExecutionDomainHolder fidl_domain_{&kAsyncLoopConfigAttachToCurrentThread};
  ExecutionDomainHolder io_domain_;
};

class ThreadingModelMixOnFidlThread : public ThreadingModelBase {
 public:
  ~ThreadingModelMixOnFidlThread() override = default;

  // |ThreadingModel|
  OwnedDomainPtr AcquireMixDomain() final {
    return OwnedDomainPtr(&FidlDomain(), [](auto* p) {});
  }
};

class ThreadingModelMixOnSingleThread : public ThreadingModelBase {
 public:
  ~ThreadingModelMixOnSingleThread() override = default;

  // |ThreadingModel|
  OwnedDomainPtr AcquireMixDomain() final {
    return OwnedDomainPtr(&mix_domain_.domain, [](auto* p) {});
  }
  void RunAndJoinAllThreads() final {
    mix_domain_.loop.StartThread("mixer");
    SetMixDispatcherThreadProfile(mix_domain_.loop.dispatcher());
    ThreadingModelBase::RunAndJoinAllThreads();
    mix_domain_.domain.PostTask([this] { mix_domain_.loop.Quit(); });
    mix_domain_.loop.JoinThreads();
  }

 private:
  ExecutionDomainHolder mix_domain_;
};

class ThreadingModelThreadPerMix : public ThreadingModelBase {
 public:
  ~ThreadingModelThreadPerMix() override = default;

  // |ThreadingModel|
  OwnedDomainPtr AcquireMixDomain() final {
    TRACE_DURATION("audio.debug", "ThreadingModelThreadPerMix::AcquireMixDomain");
    std::string thread_name;
    auto holder = std::make_unique<ExecutionDomainHolder>();
    auto dispatcher = holder->loop.dispatcher();
    auto domain = &holder->domain;
    {
      std::lock_guard<std::mutex> guard(lock_);
      if (shut_down_) {
        return nullptr;
      }
      thread_name = "mixer-" + std::to_string(mix_thread_number_++);
      holder->loop.StartThread(thread_name.c_str());
      bool inserted = mix_domains_.insert(std::make_pair(dispatcher, std::move(holder))).second;
      FX_DCHECK(inserted);
    }

    SetMixDispatcherThreadProfile(dispatcher);
    return OwnedDomainPtr(domain, [this](auto* domain) {
      TRACE_DURATION("audio.debug", "ThreadingModelThreadPerMix.delete_domain");
      // We use the IO dispatcher here because the async::Loop dtor will implicitly do a join on
      // the dispatcher threads, so we cannot run that dtor on the mix loop. Since it will block
      // while joining, we choose the io dispatcher which is there to do potentially blocking
      // operations
      auto nonce = TRACE_NONCE();
      TRACE_FLOW_BEGIN("audio.debug", "ThreadingModelThreadPerMix.release", nonce);
      IoDomain().PostTask([this, dispatcher = domain->dispatcher(), nonce] {
        TRACE_DURATION("audio.debug", "ThreadingModelThreadPerMix.release_thunk");
        TRACE_FLOW_END("audio.debug", "ThreadingModelThreadPerMix.release", nonce);
        ReleaseDomainForDispatcher(dispatcher);
      });
    });
  }
  void RunAndJoinAllThreads() final {
    ThreadingModelBase::RunAndJoinAllThreads();
    {
      std::lock_guard<std::mutex> guard(lock_);
      shut_down_ = true;
      // First post a task to each mix loop to quit these loops.
      for (auto it = mix_domains_.begin(); it != mix_domains_.end(); ++it) {
        async::PostTask(it->first, [loop = &it->second->loop] { loop->Quit(); });
      }
      // Now just wait for all in-flight tasks to complete.
      for (auto it = mix_domains_.begin(); it != mix_domains_.end(); ++it) {
        it->second->loop.JoinThreads();
      }
    }
  }

 private:
  void ReleaseDomainForDispatcher(async_dispatcher_t* dispatcher) {
    TRACE_DURATION("audio.debug", "ThreadingModelThreadPerMix::ReleaseDomainForDispatcher");
    std::unique_ptr<ExecutionDomainHolder> domain;
    {
      std::lock_guard<std::mutex> guard(lock_);
      auto it = mix_domains_.find(dispatcher);
      FX_DCHECK(it != mix_domains_.end());
      domain = std::move(it->second);
      mix_domains_.erase(it);
    }

    domain->loop.Shutdown();
  }

  std::mutex lock_;
  bool shut_down_ __TA_GUARDED(lock_) = false;
  std::unordered_map<async_dispatcher_t*, std::unique_ptr<ExecutionDomainHolder>> mix_domains_
      __TA_GUARDED(lock_);
  uint32_t mix_thread_number_ = 0;
};

}  // namespace

std::unique_ptr<ThreadingModel> ThreadingModel::CreateWithMixStrategy(MixStrategy mix_strategy) {
  switch (mix_strategy) {
    case MixStrategy::kMixOnFidlThread:
      return std::make_unique<ThreadingModelMixOnFidlThread>();
    case MixStrategy::kMixOnSingleThread:
      return std::make_unique<ThreadingModelMixOnSingleThread>();
    case MixStrategy::kThreadPerMix:
      return std::make_unique<ThreadingModelThreadPerMix>();
  }
}

}  // namespace media::audio
