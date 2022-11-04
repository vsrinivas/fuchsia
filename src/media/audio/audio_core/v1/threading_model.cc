// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/threading_model.h"

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

#include "src/media/audio/audio_core/v1/mix_profile_config.h"
#include "src/media/audio/audio_core/v1/utils.h"

namespace media::audio {
namespace {

void SetMixDispatcherThreadProfile(const MixProfileConfig& mix_profile_config,
                                   async_dispatcher_t* dispatcher) {
  zx::profile profile;
  zx_status_t status = AcquireHighPriorityProfile(mix_profile_config, &profile);
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
  explicit ExecutionDomainHolder(const std::string& domain_name)
      : ExecutionDomainHolder(&kAsyncLoopConfigNoAttachToCurrentThread, domain_name) {}

  ExecutionDomainHolder(const async_loop_config_t* loop_config, const std::string& domain_name)
      : loop{loop_config},
        executor{loop.dispatcher()},
        domain{loop.dispatcher(), &executor, domain_name} {}

  async::Loop loop;
  async::Executor executor;
  ExecutionDomain domain;
};

class ThreadingModelBase : public ThreadingModel {
 public:
  explicit ThreadingModelBase(MixProfileConfig mix_profile_config)
      : mix_profile_config_(mix_profile_config) {}

  ~ThreadingModelBase() override = default;

  // |ThreadingModel|
  ExecutionDomain& FidlDomain() final { return fidl_domain_.domain; }
  ExecutionDomain& IoDomain() final { return io_domain_.domain; }
  void RunAndJoinAllThreads() override {
    io_domain_.loop.StartThread(io_domain_.domain.name().c_str());
    fidl_domain_.loop.Run();
    IoDomain().PostTask([this] { io_domain_.loop.Quit(); });
    io_domain_.loop.JoinThreads();
  }
  void Quit() final {
    FidlDomain().PostTask([this] { fidl_domain_.loop.Quit(); });
  }

  const MixProfileConfig& mix_profile_config() const { return mix_profile_config_; }

 private:
  ExecutionDomainHolder fidl_domain_{&kAsyncLoopConfigAttachToCurrentThread, "fidl"};
  ExecutionDomainHolder io_domain_{"io"};
  MixProfileConfig mix_profile_config_;
};

class ThreadingModelMixOnFidlThread : public ThreadingModelBase {
 public:
  explicit ThreadingModelMixOnFidlThread(MixProfileConfig mix_profile_config)
      : ThreadingModelBase(mix_profile_config) {}
  ~ThreadingModelMixOnFidlThread() override = default;

  // |ThreadingModel|
  OwnedDomainPtr AcquireMixDomain(const std::string& name_hint) final {
    return OwnedDomainPtr(&FidlDomain(), [](auto* p) {});
  }
};

class ThreadingModelMixOnSingleThread : public ThreadingModelBase {
 public:
  explicit ThreadingModelMixOnSingleThread(MixProfileConfig mix_profile_config)
      : ThreadingModelBase(mix_profile_config) {}
  ~ThreadingModelMixOnSingleThread() override = default;

  // |ThreadingModel|
  OwnedDomainPtr AcquireMixDomain(const std::string& name_hint) final {
    return OwnedDomainPtr(&mix_domain_.domain, [](auto* p) {});
  }

  void RunAndJoinAllThreads() final {
    mix_domain_.loop.StartThread(mix_domain_.domain.name().c_str());
    SetMixDispatcherThreadProfile(mix_profile_config(), mix_domain_.loop.dispatcher());
    ThreadingModelBase::RunAndJoinAllThreads();
    mix_domain_.domain.PostTask([this] { mix_domain_.loop.Quit(); });
    mix_domain_.loop.JoinThreads();
  }

 private:
  ExecutionDomainHolder mix_domain_{"mixer"};
};

class ThreadingModelThreadPerMix : public ThreadingModelBase {
 public:
  explicit ThreadingModelThreadPerMix(MixProfileConfig mix_profile_config)
      : ThreadingModelBase(mix_profile_config) {}
  ~ThreadingModelThreadPerMix() override = default;

  // |ThreadingModel|
  OwnedDomainPtr AcquireMixDomain(const std::string& name_hint) final {
    TRACE_DURATION("audio.debug", "ThreadingModelThreadPerMix::AcquireMixDomain");
    std::unique_ptr<ExecutionDomainHolder> holder;
    async_dispatcher_t* dispatcher;
    ExecutionDomain* domain;
    {
      std::lock_guard<std::mutex> guard(lock_);
      if (shut_down_) {
        return nullptr;
      }
      std::string thread_name = "mixer-" + name_hint + "-" + std::to_string(mix_thread_number_++);
      holder = std::make_unique<ExecutionDomainHolder>(thread_name);
      holder->loop.StartThread(thread_name.c_str());
      dispatcher = holder->loop.dispatcher();
      domain = &holder->domain;
      bool inserted = mix_domains_.insert(std::make_pair(dispatcher, std::move(holder))).second;
      FX_DCHECK(inserted);
    }

    SetMixDispatcherThreadProfile(mix_profile_config(), dispatcher);
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

std::unique_ptr<ThreadingModel> ThreadingModel::CreateWithMixStrategy(
    MixStrategy mix_strategy, MixProfileConfig mix_profile_config) {
  switch (mix_strategy) {
    case MixStrategy::kMixOnFidlThread:
      return std::make_unique<ThreadingModelMixOnFidlThread>(mix_profile_config);
    case MixStrategy::kMixOnSingleThread:
      return std::make_unique<ThreadingModelMixOnSingleThread>(mix_profile_config);
    case MixStrategy::kThreadPerMix:
      return std::make_unique<ThreadingModelThreadPerMix>(mix_profile_config);
  }
}

}  // namespace media::audio
