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
      chanmgr_ = std::make_unique<ChannelManager>(hci_, dispatcher());

      FXL_VLOG(1) << "l2cap: Initialized";
    });
  }

  void ShutDown() override {
    common::TaskDomain<Impl, L2CAP>::ScheduleCleanUp();
  }

  // Called on the L2CAP runner as a result of ScheduleCleanUp().
  void CleanUp() {
    AssertOnDispatcherThread();
    FXL_VLOG(1) << "l2cap: Shutting down";
    chanmgr_ = nullptr;
  }

  void AddACLConnection(hci::ConnectionHandle handle,
                        hci::Connection::Role role,
                        LinkErrorCallback link_error_callback,
                        async_dispatcher_t* dispatcher) override {
    PostMessage([this, handle, role, lec = std::move(link_error_callback),
                 dispatcher]() mutable {
      if (chanmgr_) {
        chanmgr_->RegisterACL(handle, role, std::move(lec), dispatcher);
      }
    });
  }

  void AddLEConnection(hci::ConnectionHandle handle, hci::Connection::Role role,
                       LEConnectionParameterUpdateCallback conn_param_callback,
                       LinkErrorCallback link_error_callback,
                       AddLEConnectionCallback channel_callback,
                       async_dispatcher_t* dispatcher) override {
    PostMessage([this, handle, role, cp_cb = std::move(conn_param_callback),
                 link_err_cb = std::move(link_error_callback),
                 chan_cb = std::move(channel_callback), dispatcher]() mutable {
      if (chanmgr_) {
        chanmgr_->RegisterLE(handle, role, std::move(cp_cb),
                             std::move(link_err_cb), dispatcher);

        auto att = chanmgr_->OpenFixedChannel(handle, kATTChannelId);
        auto smp = chanmgr_->OpenFixedChannel(handle, kLESMPChannelId);
        FXL_DCHECK(att);
        FXL_DCHECK(smp);
        async::PostTask(dispatcher, [att = std::move(att), smp = std::move(smp),
                                     cb = std::move(chan_cb)]() mutable {
          cb(std::move(att), std::move(smp));
        });
      }
    });
  }

  void RemoveConnection(hci::ConnectionHandle handle) override {
    PostMessage([this, handle] {
      if (chanmgr_) {
        chanmgr_->Unregister(handle);
      }
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
