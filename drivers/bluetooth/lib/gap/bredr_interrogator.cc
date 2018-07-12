// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bredr_interrogator.h"

#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "garnet/drivers/bluetooth/lib/hci/transport.h"

namespace btlib {
namespace gap {

namespace {

// The maximum features page that we'll attempt to retrieve.
constexpr uint8_t kMaxPage = 2;

}  // namespace

BrEdrInterrogator::Interrogation::Interrogation(hci::ConnectionPtr conn,
                                                ResultCallback cb)
    : conn_ptr(std::move(conn)), result_cb(std::move(cb)) {
  FXL_DCHECK(conn_ptr);
  FXL_DCHECK(result_cb);
}

BrEdrInterrogator::Interrogation::~Interrogation() {
  Finish(hci::Status(common::HostError::kFailed));
}

void BrEdrInterrogator::Interrogation::Finish(hci::Status status) {
  // If the connection is gone, we are finished already.
  if (!conn_ptr) {
    return;
  }
  // Cancel any callbacks we might receive.
  callbacks.clear();

  result_cb(status, std::move(conn_ptr));
}

BrEdrInterrogator::BrEdrInterrogator(RemoteDeviceCache* cache,
                                     fxl::RefPtr<hci::Transport> hci,
                                     async_dispatcher_t* dispatcher)
    : hci_(hci),
      dispatcher_(dispatcher),
      cache_(cache),
      weak_ptr_factory_(this) {
  FXL_DCHECK(hci_);
  FXL_DCHECK(dispatcher_);
  FXL_DCHECK(cache_);
}

BrEdrInterrogator::~BrEdrInterrogator() {
  for (auto& p : pending_) {
    Cancel(p.first);
  }
}

void BrEdrInterrogator::Start(const std::string& device_id,
                              hci::ConnectionPtr conn_ptr,
                              ResultCallback callback) {
  FXL_DCHECK(conn_ptr);
  FXL_DCHECK(callback);

  hci::ConnectionHandle handle = conn_ptr->handle();

  pending_.emplace(device_id, std::make_unique<Interrogation>(
                                  std::move(conn_ptr), std::move(callback)));

  RemoteDevice* device = cache_->FindDeviceById(device_id);
  if (!device) {
    Complete(device_id, hci::Status(common::HostError::kFailed));
    return;
  }

  if (!device->name()) {
    MakeRemoteNameRequest(device_id);
  }

  if (!device->version()) {
    ReadRemoteVersionInformation(device_id, handle);
  }

  if (!device->features().HasPage(0)) {
    ReadRemoteFeatures(device_id, handle);
  } else if (device->features().HasBit(0, hci::LMPFeature::kExtendedFeatures)) {
    for (size_t page = 1; page <= hci::LMPFeatureSet::kMaxPages; page++) {
      ReadRemoteExtendedFeatures(device_id, handle, page);
    }
  }
}

void BrEdrInterrogator::Cancel(std::string device_id) {
  async::PostTask(dispatcher_, [id = std::move(device_id),
                                self = weak_ptr_factory_.GetWeakPtr()]() {
    if (!self) {
      return;
    }

    auto it = self->pending_.find(id);
    if (it == self->pending_.end()) {
      return;
    }

    it->second->Finish(hci::Status(common::HostError::kCanceled));
    self->pending_.erase(it);
  });
}

void BrEdrInterrogator::MaybeComplete(const std::string& device_id) {
  RemoteDevice* device = cache_->FindDeviceById(device_id);
  if (!device) {
    Complete(device_id, hci::Status(common::HostError::kFailed));
    return;
  }
  if (!device->name()) {
    return;
  }

  if (!device->version()) {
    return;
  }

  if (!device->features().HasPage(0)) {
    return;
  } else if (device->features().HasBit(0, hci::LMPFeature::kExtendedFeatures)) {
    for (uint8_t page = 1; page <= kMaxPage; page++) {
      if (!device->features().HasPage(page)) {
        return;
      }
    }
  }

  Complete(device_id, hci::Status());
}

void BrEdrInterrogator::Complete(std::string device_id, hci::Status status) {
  auto it = pending_.find(std::move(device_id));
  FXL_DCHECK(it != pending_.end());

  it->second->Finish(std::move(status));
  pending_.erase(it);
}

void BrEdrInterrogator::MakeRemoteNameRequest(const std::string& device_id) {
  RemoteDevice* device = cache_->FindDeviceById(device_id);
  if (!device) {
    Complete(device_id, hci::Status(common::HostError::kFailed));
    return;
  }
  hci::PageScanRepetitionMode mode = hci::PageScanRepetitionMode::kR0;
  if (device->page_scan_repetition_mode()) {
    mode = *device->page_scan_repetition_mode();
  }
  auto packet = hci::CommandPacket::New(
      hci::kRemoteNameRequest, sizeof(hci::RemoteNameRequestCommandParams));
  packet->mutable_view()->mutable_payload_data().SetToZeros();
  auto params = packet->mutable_view()
                    ->mutable_payload<hci::RemoteNameRequestCommandParams>();
  params->bd_addr = device->address().value();
  params->page_scan_repetition_mode = mode;
  if (device->clock_offset()) {
    params->clock_offset = *(device->clock_offset());
  }

  auto it = pending_.find(device_id);
  FXL_DCHECK(it != pending_.end());

  it->second->callbacks.emplace_back(
      [device_id, self = weak_ptr_factory_.GetWeakPtr()](auto,
                                                         const auto& event) {
        if (BTEV_TEST_LOG(event, INFO,
                          "gap (BR/EDR): RemoteNameRequest failed")) {
          self->Complete(device_id, event.ToStatus());
          return;
        }

        if (event.event_code() == hci::kCommandStatusEventCode) {
          return;
        }

        FXL_DCHECK(event.event_code() ==
                   hci::kRemoteNameRequestCompleteEventCode);

        const auto& params =
            event.view()
                .template payload<hci::RemoteNameRequestCompleteEventParams>();

        size_t len = 0;
        for (; len < hci::kMaxNameLength; len++) {
          if (params.remote_name[len] == 0) {
            break;
          }
        }
        RemoteDevice* device = self->cache_->FindDeviceById(device_id);
        if (!device) {
          self->Complete(device_id, hci::Status(common::HostError::kFailed));
          return;
        }
        device->SetName(
            std::string(params.remote_name, params.remote_name + len));

        self->MaybeComplete(device_id);
      });

  hci_->command_channel()->SendCommand(
      std::move(packet), dispatcher_, it->second->callbacks.back().callback(),
      hci::kRemoteNameRequestCompleteEventCode);
}

void BrEdrInterrogator::ReadRemoteVersionInformation(
    const std::string& device_id, hci::ConnectionHandle handle) {
  auto packet =
      hci::CommandPacket::New(hci::kReadRemoteVersionInfo,
                              sizeof(hci::ReadRemoteVersionInfoCommandParams));
  packet->mutable_view()
      ->mutable_payload<hci::ReadRemoteVersionInfoCommandParams>()
      ->connection_handle = htole16(handle);

  auto it = pending_.find(device_id);
  FXL_DCHECK(it != pending_.end());

  it->second->callbacks.emplace_back([device_id,
                                      self = weak_ptr_factory_.GetWeakPtr()](
                                         auto, const auto& event) {
    if (BTEV_TEST_LOG(event, INFO,
                      "gap (BR/EDR): ReadRemoteVersionInfo failed")) {
      self->Complete(device_id, event.ToStatus());
      return;
    }

    if (event.event_code() == hci::kCommandStatusEventCode) {
      return;
    }

    FXL_DCHECK(event.event_code() ==
               hci::kReadRemoteVersionInfoCompleteEventCode);

    const auto params =
        event.view()
            .template payload<hci::ReadRemoteVersionInfoCompleteEventParams>();

    RemoteDevice* device = self->cache_->FindDeviceById(device_id);
    if (!device) {
      self->Complete(device_id, hci::Status(common::HostError::kFailed));
      return;
    }
    device->set_version(params.lmp_version, params.manufacturer_name,
                        params.lmp_subversion);

    self->MaybeComplete(device_id);
  });

  hci_->command_channel()->SendCommand(
      std::move(packet), dispatcher_, it->second->callbacks.back().callback(),
      hci::kReadRemoteVersionInfoCompleteEventCode);
}

void BrEdrInterrogator::ReadRemoteFeatures(const std::string& device_id,
                                           hci::ConnectionHandle handle) {
  auto packet = hci::CommandPacket::New(
      hci::kReadRemoteSupportedFeatures,
      sizeof(hci::ReadRemoteSupportedFeaturesCommandParams));
  packet->mutable_view()
      ->mutable_payload<hci::ReadRemoteVersionInfoCommandParams>()
      ->connection_handle = htole16(handle);

  auto it = pending_.find(device_id);
  FXL_DCHECK(it != pending_.end());

  it->second->callbacks.emplace_back(
      [device_id, handle, self = weak_ptr_factory_.GetWeakPtr()](
          auto, const auto& event) {
        if (BTEV_TEST_LOG(event, INFO,
                          "gap (BR/EDR): ReadRemoteSupportedFeatures failed")) {
          self->Complete(device_id, event.ToStatus());
          return;
        }

        if (event.event_code() == hci::kCommandStatusEventCode) {
          return;
        }

        FXL_DCHECK(event.event_code() ==
                   hci::kReadRemoteSupportedFeaturesCompleteEventCode);

        const auto& params =
            event.view()
                .template payload<
                    hci::ReadRemoteSupportedFeaturesCompleteEventParams>();

        RemoteDevice* device = self->cache_->FindDeviceById(device_id);
        if (!device) {
          self->Complete(device_id, hci::Status(common::HostError::kFailed));
          return;
        }
        device->SetFeaturePage(0, le64toh(params.lmp_features));

        if (device->features().HasBit(0, hci::LMPFeature::kExtendedFeatures)) {
          for (uint8_t page = 1; page <= kMaxPage; page++) {
            self->ReadRemoteExtendedFeatures(device_id, handle, page);
          }
        }

        self->MaybeComplete(device_id);
      });

  hci_->command_channel()->SendCommand(
      std::move(packet), dispatcher_, it->second->callbacks.back().callback(),
      hci::kReadRemoteSupportedFeaturesCompleteEventCode);
}

void BrEdrInterrogator::ReadRemoteExtendedFeatures(const std::string& device_id,
                                                   hci::ConnectionHandle handle,
                                                   uint8_t page) {
  auto packet = hci::CommandPacket::New(
      hci::kReadRemoteExtendedFeatures,
      sizeof(hci::ReadRemoteExtendedFeaturesCommandParams));
  auto params =
      packet->mutable_view()
          ->mutable_payload<hci::ReadRemoteExtendedFeaturesCommandParams>();
  params->connection_handle = htole16(handle);
  params->page_number = page;

  auto it = pending_.find(device_id);
  FXL_DCHECK(it != pending_.end());

  it->second->callbacks.emplace_back(
      [device_id, self = weak_ptr_factory_.GetWeakPtr()](auto,
                                                         const auto& event) {
        if (BTEV_TEST_LOG(event, INFO,
                          "gap (BR/EDR): ReadRemoteExtendedFeatures failed")) {
          self->Complete(device_id, event.ToStatus());
          return;
        }

        if (event.event_code() == hci::kCommandStatusEventCode) {
          return;
        }

        FXL_DCHECK(event.event_code() ==
                   hci::kReadRemoteExtendedFeaturesCompleteEventCode);

        const auto& params =
            event.view()
                .template payload<
                    hci::ReadRemoteExtendedFeaturesCompleteEventParams>();

        RemoteDevice* device = self->cache_->FindDeviceById(device_id);
        if (!device) {
          self->Complete(device_id, hci::Status(common::HostError::kFailed));
          return;
        }
        device->SetFeaturePage(params.page_number,
                               le64toh(params.lmp_features));

        self->MaybeComplete(device_id);
      });

  hci_->command_channel()->SendCommand(
      std::move(packet), dispatcher_, it->second->callbacks.back().callback(),
      hci::kReadRemoteExtendedFeaturesCompleteEventCode);
}

}  // namespace gap
}  // namespace btlib
