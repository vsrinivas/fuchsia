// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt.h"

#include <unordered_map>

#include "garnet/drivers/bluetooth/lib/att/bearer.h"
#include "garnet/drivers/bluetooth/lib/common/task_domain.h"
#include "garnet/drivers/bluetooth/lib/gatt/generic_attribute_service.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"

#include "client.h"
#include "connection.h"
#include "remote_service.h"
#include "server.h"

namespace btlib {
namespace gatt {
namespace {

class Impl final : public GATT, common::TaskDomain<Impl, GATT> {
  using TaskDomainBase = common::TaskDomain<Impl, GATT>;

 public:
  explicit Impl(async_dispatcher_t* gatt_dispatcher)
      : TaskDomainBase(this, gatt_dispatcher), initialized_(false) {}

  ~Impl() override {
    FXL_DCHECK(!initialized_) << "gatt: ShutDown() must have been called!";
  }

  // GATT overrides:
  void Initialize() override {
    PostMessage([this] {
      FXL_DCHECK(!initialized_);

      local_services_ = std::make_unique<LocalServiceManager>();

      // Forwards Service Changed payloads to clients.
      auto send_indication_callback = [this](const std::string& peer_id,
                                             att::Handle handle,
                                             const common::ByteBuffer& value) {
        auto iter = connections_.find(peer_id);
        if (iter == connections_.end()) {
          FXL_LOG(WARNING) << "gatt: Peer not registered: " << peer_id;
          return;
        }
        iter->second.server()->SendNotification(handle, value.view(), true);
      };

      // Spin up Generic Attribute as the first service.
      gatt_service_ = std::make_unique<GenericAttributeService>(
          local_services_.get(), std::move(send_indication_callback));

      initialized_ = true;

      FXL_VLOG(1) << "gatt: initialized";
    });
  }

  void ShutDown() override { TaskDomainBase::ScheduleCleanUp(); }

  // Called on the GATT runner as a result of ScheduleCleanUp().
  void CleanUp() {
    FXL_VLOG(1) << "gatt: shutting down";

    initialized_ = false;
    connections_.clear();
    gatt_service_ = nullptr;
    local_services_ = nullptr;
    remote_service_callbacks_.clear();
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

      auto att_bearer = att::Bearer::Create(att_chan);
      if (!att_bearer) {
        // This can happen if the link closes before the Bearer activates the
        // channel.
        FXL_LOG(ERROR) << "gatt: Failed to initialize ATT bearer";
        att_chan->SignalLinkError();
        return;
      }

      connections_[peer_id] =
          internal::Connection(peer_id, att_bearer, local_services_->database(),
                               std::bind(&Impl::OnServiceAdded, this, peer_id,
                                         std::placeholders::_1),
                               dispatcher());
    });
  }

  void RemoveConnection(std::string peer_id) override {
    FXL_VLOG(1) << "gatt: Remove connection: " << peer_id;
    PostMessage(
        [this, peer_id = std::move(peer_id)] {
            local_services_->DisconnectClient(peer_id);
            connections_.erase(peer_id);
        });
  }

  void RegisterService(ServicePtr service,
                       ServiceIdCallback callback,
                       ReadHandler read_handler,
                       WriteHandler write_handler,
                       ClientConfigCallback ccc_callback) override {
    PostMessage([this, svc = std::move(service), id_cb = std::move(callback),
                 rh = std::move(read_handler), wh = std::move(write_handler),
                 cccc = std::move(ccc_callback)]() mutable {
      IdType id;

      if (!initialized_) {
        FXL_VLOG(1) << "gatt: Cannot register service after shutdown";
        id = kInvalidId;
      } else {
        id = local_services_->RegisterService(std::move(svc), std::move(rh),
                                              std::move(wh), std::move(cccc));
      }

      id_cb(id);
    });
  }

  void UnregisterService(IdType service_id) override {
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
                        ::fidl::VectorPtr<uint8_t> value,
                        bool indicate) override {
    PostMessage([this, svc_id = service_id, chrc_id, indicate,
                 peer_id = std::move(peer_id), value = value.take()] {
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
          config.handle, common::BufferView(value.data(), value.size()),
          indicate);
    });
  }

  void DiscoverServices(std::string peer_id) override {
    FXL_VLOG(1) << "gatt: Discover services: " << peer_id;

    PostMessage([this, peer_id = std::move(peer_id)] {
      auto iter = connections_.find(peer_id);
      if (iter == connections_.end()) {
        FXL_LOG(WARNING) << "gatt: Unknown peer: " << peer_id;
        return;
      }

      iter->second.Initialize();
    });
  }

  void RegisterRemoteServiceWatcher(RemoteServiceWatcher callback,
                                    async_dispatcher_t* dispatcher) override {
    FXL_DCHECK(callback);
    PostMessage(
        [this, callback = std::move(callback), runner = dispatcher]() mutable {
          if (initialized_) {
            remote_service_callbacks_.emplace_back(std::move(callback),
                                                   std::move(runner));
          }
        });
  }

  void ListServices(std::string peer_id,
                    std::vector<common::UUID> uuids,
                    ServiceListCallback callback) override {
    FXL_DCHECK(callback);
    PostMessage([this, peer_id = std::move(peer_id),
                 callback = std::move(callback),
                 uuids = std::move(uuids)]() mutable {
      auto iter = connections_.find(peer_id);
      if (iter == connections_.end()) {
        // Connection not found.
        callback(att::Status(common::HostError::kNotFound), ServiceList());
        return;
      }
      iter->second.remote_service_manager()->ListServices(uuids,
                                                          std::move(callback));
    });
  }

  void FindService(std::string peer_id,
                   IdType service_id,
                   RemoteServiceCallback callback) override {
    PostMessage([this, service_id, peer_id = std::move(peer_id),
                 callback = std::move(callback)]() mutable {
      auto iter = connections_.find(peer_id);
      if (iter == connections_.end()) {
        // Connection not found.
        callback(nullptr);
        return;
      }
      callback(iter->second.remote_service_manager()->FindService(service_id));
    });
  }

 private:
  // Called when a new remote GATT service is discovered.
  void OnServiceAdded(const std::string& peer_id,
                      fbl::RefPtr<RemoteService> svc) {
    FXL_VLOG(1) << "gatt: Service added (peer_id: " << peer_id
                << ", handle: " << svc->handle()
                << ", uuid: " << svc->uuid().ToString();
    for (auto& handler : remote_service_callbacks_) {
      handler.Notify(peer_id, svc);
    }
  }

  // NOTE: The following objects MUST be initialized, accessed, and destroyed on
  // the GATT thread. They are not thread safe.
  bool initialized_;

  // The registry containing all local GATT services. This represents a single
  // ATT database.
  std::unique_ptr<LocalServiceManager> local_services_;

  // Local GATT service (first in database) for clients to subscribe to service
  // registration and removal.
  std::unique_ptr<GenericAttributeService> gatt_service_;

  // Contains the state of all GATT profile connections and their services.
  std::unordered_map<std::string, internal::Connection> connections_;

  // All registered remote service handlers.
  struct RemoteServiceHandler {
    RemoteServiceHandler(RemoteServiceWatcher watcher, async_dispatcher_t* dispatcher)
        : watcher_(std::move(watcher)), dispatcher_(dispatcher) {}

    RemoteServiceHandler() = default;
    RemoteServiceHandler(RemoteServiceHandler&&) = default;

    void Notify(const std::string& peer_id, fbl::RefPtr<RemoteService> svc) {
      if (!dispatcher_) {
        watcher_(peer_id, std::move(svc));
        return;
      }

      // NOTE: this makes a copy of |watcher_|.
      async::PostTask(dispatcher_, [peer_id, watcher = watcher_.share(),
                                    svc = std::move(svc)]() mutable {
        watcher(peer_id, std::move(svc));
      });
    }

   private:
    RemoteServiceWatcher watcher_;
    async_dispatcher_t* dispatcher_;

    FXL_DISALLOW_COPY_AND_ASSIGN(RemoteServiceHandler);
  };

  std::vector<RemoteServiceHandler> remote_service_callbacks_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Impl);
};

}  // namespace

// static
fbl::RefPtr<GATT> GATT::Create(async_dispatcher_t* gatt_dispatcher) {
  return AdoptRef(new Impl(gatt_dispatcher));
}

}  // namespace gatt
}  // namespace btlib
