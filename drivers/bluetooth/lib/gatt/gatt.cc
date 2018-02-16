// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt.h"

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/common/task_domain.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"

#include "lib/fxl/functional/make_copyable.h"

#include "connection.h"
#include "server.h"

namespace btlib {
namespace gatt {
namespace {

class Impl final : public GATT, common::TaskDomain<Impl, GATT> {
  using TaskDomainBase = common::TaskDomain<Impl, GATT>;

 public:
  explicit Impl(fxl::RefPtr<fxl::TaskRunner> gatt_runner)
      : TaskDomainBase(this, gatt_runner), initialized_(false) {}

  ~Impl() override {
    FXL_DCHECK(!initialized_) << "gatt: ShutDown() must have been called!";
  }

  // GATT overrides:
  void Initialize() override {
    PostMessage([this] {
      FXL_DCHECK(!initialized_);

      local_services_ = std::make_unique<LocalServiceManager>();
      initialized_ = true;

      FXL_VLOG(1) << "gatt: initialized";
    });
  }

  void ShutDown() override { TaskDomainBase::ScheduleCleanUp(); }

  // Called on the GATT runner as a result of ScheduleCleanUp().
  void CleanUp() {
    FXL_DCHECK(task_runner()->RunsTasksOnCurrentThread());
    FXL_VLOG(1) << "gatt: shutting down";

    initialized_ = false;
    connections_.clear();
    local_services_ = nullptr;
  }

  void AddConnection(const std::string& peer_id,
                     fbl::RefPtr<l2cap::Channel> att_chan) override {
    FXL_VLOG(1) << "gatt: Add connection: " << peer_id;

    PostMessage([this, peer_id, att_chan] {
      FXL_DCHECK(local_services_);

      auto iter = connections_.find(peer_id);
      if (iter != connections_.end()) {
        FXL_LOG(WARNING) << "gatt: Peer is already registered: " << peer_id;
        return;
      }

      connections_[peer_id] =
          internal::Connection(peer_id, att_chan, local_services_->database());
    });
  }

  void RemoveConnection(std::string peer_id) override {
    FXL_VLOG(1) << "gatt: Remove connection: " << peer_id;
    PostMessage(
        [this, peer_id = std::move(peer_id)] { connections_.erase(peer_id); });
  }

  void RegisterService(ServicePtr service,
                       ServiceIdCallback callback,
                       ReadHandler read_handler,
                       WriteHandler write_handler,
                       ClientConfigCallback ccc_callback,
                       fxl::RefPtr<fxl::TaskRunner> task_runner) override {
    PostMessage(fxl::MakeCopyable(
        [this, svc = std::move(service), id_cb = std::move(callback),
         rh = std::move(read_handler), wh = std::move(write_handler),
         cccc = std::move(ccc_callback), task_runner]() mutable {
          IdType id;

          if (!initialized_) {
            FXL_VLOG(1) << "gatt: Cannot register service after shutdown";
            id = kInvalidId;
          } else {
            id = local_services_->RegisterService(
                std::move(svc), std::move(rh), std::move(wh), std::move(cccc));
          }

          if (task_runner->RunsTasksOnCurrentThread()) {
            id_cb(id);
          } else {
            task_runner->PostTask(
                [id, id_cb = std::move(id_cb)] { id_cb(id); });
          }
        }));
  }

  void UnregisterService(IdType service_id) override {
    FXL_DCHECK(task_runner()->RunsTasksOnCurrentThread());
    PostMessage([this, service_id] {
      if (!initialized_)
        return;

      FXL_DCHECK(local_services_);
      local_services_->UnregisterService(service_id);
    });
  }

  void SendNotification(IdType service_id,
                        IdType chrc_id,
                        std::string peer_id,
                        ::f1dl::Array<uint8_t> value,
                        bool indicate) override {
    FXL_DCHECK(task_runner()->RunsTasksOnCurrentThread());

    std::vector<uint8_t> vec;
    value->swap(vec);

    PostMessage([this, svc_id = service_id, chrc_id, indicate,
                 peer_id = std::move(peer_id), vec = std::move(vec)] {
      if (!initialized_) {
        FXL_VLOG(3) << "gatt: Cannot notify after shutdown";
        return;
      }

      // There is nothing to do if the requested peer is not connected.
      auto iter = connections_.find(peer_id);
      if (iter == connections_.end()) {
        FXL_VLOG(2) << "gatt: Cannot notify disconnected peer: " << peer_id;
        return;
      }

      LocalServiceManager::ClientCharacteristicConfig config;
      if (!local_services_->GetCharacteristicConfig(svc_id, chrc_id, peer_id,
                                                    &config)) {
        FXL_VLOG(2) << "gatt: Peer has not configured characteristic: "
                    << peer_id;
        return;
      }

      // Make sure that the client has subscribed to the requested protocol
      // method.
      if ((indicate & !config.indicate) || (!indicate && !config.notify)) {
        FXL_VLOG(2) << "gatt: Peer has no configuration ("
                    << (indicate ? "ind" : "not") << "): " << peer_id;
        return;
      }

      iter->second.server()->SendNotification(
          config.handle, common::BufferView(vec.data(), vec.size()), indicate);
    });
  }

 private:
  // NOTE: The following objects MUST be initialized, accessed, and destroyed on
  // the GATT thread. They are not thread safe.
  bool initialized_;

  // The registry containing all local GATT services. This represents a single
  // ATT database.
  std::unique_ptr<LocalServiceManager> local_services_;

  // Contains the state of all GATT profile connections and their services.
  std::unordered_map<std::string, gatt::internal::Connection> connections_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

}  // namespace

// static
fbl::RefPtr<GATT> GATT::Create(fxl::RefPtr<fxl::TaskRunner> gatt_runner) {
  FXL_DCHECK(gatt_runner);
  return AdoptRef(new Impl(gatt_runner));
}

}  // namespace gatt
}  // namespace btlib
