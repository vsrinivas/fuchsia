// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "l2cap.h"

#include "garnet/drivers/bluetooth/lib/common/task_domain.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

#include "channel_manager.h"

namespace btlib {
namespace l2cap {
namespace {

class Impl final : public L2CAP, public common::TaskDomain<Impl, L2CAP> {
 public:
  Impl(fxl::RefPtr<hci::Transport> hci, std::string thread_name)
      : L2CAP(),
        common::TaskDomain<Impl, L2CAP>(this, std::move(thread_name)),
        hci_(hci) {
    FXL_DCHECK(hci_);
  }

  void Initialize() override {
    PostMessage([this] {
      // This can only run once during initialization.
      FXL_DCHECK(!chanmgr_);
      chanmgr_ = std::make_unique<ChannelManager>(hci_, task_runner());

      FXL_VLOG(1) << "l2cap: Initialized";
    });
  }

  void ShutDown() override {
    common::TaskDomain<Impl, L2CAP>::ScheduleCleanUp();
  }

  // Called on the L2CAP runner as a result of ScheduleCleanUp().
  void CleanUp() {
    FXL_DCHECK(task_runner()->RunsTasksOnCurrentThread());
    FXL_VLOG(1) << "l2cap: Shutting down";
    chanmgr_ = nullptr;
  }

  void RegisterLE(hci::ConnectionHandle handle,
                  hci::Connection::Role role,
                  const LEConnectionParameterUpdateCallback& callback,
                  fxl::RefPtr<fxl::TaskRunner> task_runner) override {
    PostMessage([this, handle, role, callback, task_runner] {
      if (chanmgr_) {
        chanmgr_->RegisterLE(handle, role, callback, task_runner);
      }
    });
  }

  void Unregister(hci::ConnectionHandle handle) override {
    PostMessage([this, handle] {
      if (chanmgr_) {
        chanmgr_->Unregister(handle);
      }
    });
  }

  void OpenFixedChannel(hci::ConnectionHandle handle,
                        ChannelId id,
                        ChannelCallback callback,
                        fxl::RefPtr<fxl::TaskRunner> callback_runner) override {
    PostMessage([this, handle, id, cb = std::move(callback),
                 callback_runner]() mutable {
      if (!chanmgr_)
        return;

      auto chan = chanmgr_->OpenFixedChannel(handle, id);
      callback_runner->PostTask([chan, cb = std::move(cb)] { cb(chan); });
    });
  }

 private:
  fxl::RefPtr<hci::Transport> hci_;

  // This must be only accessed on the L2CAP task runner.
  std::unique_ptr<ChannelManager> chanmgr_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

}  // namespace

// static
fbl::RefPtr<L2CAP> L2CAP::Create(fxl::RefPtr<hci::Transport> hci,
                                 std::string thread_name) {
  FXL_DCHECK(hci);

  return AdoptRef(new Impl(hci, std::move(thread_name)));
}

}  // namespace l2cap
}  // namespace btlib
