// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_interface.h"

#include <array>

#include <fbl/alloc_checker.h>

#include "log.h"

namespace network {

zx_status_t MacAddrDeviceInterface::Create(ddk::MacAddrImplProtocolClient parent,
                                           std::unique_ptr<MacAddrDeviceInterface>* out) {
  std::unique_ptr<internal::MacInterface> mac;
  zx_status_t status = internal::MacInterface::Create(parent, &mac);
  if (status == ZX_OK) {
    *out = std::move(mac);
  }
  return status;
}

namespace internal {

constexpr uint8_t kMacMulticast = 0x01;

// We make some assumptions in the logic about the ordering of the constants defined in the
// protocol. If that's not true we want a compilation failure.
static_assert(MODE_MULTICAST_PROMISCUOUS == MODE_MULTICAST_FILTER << 1u);
static_assert(MODE_PROMISCUOUS == MODE_MULTICAST_PROMISCUOUS << 1u);

MacInterface::MacInterface(ddk::MacAddrImplProtocolClient parent) : impl_(parent) {}

MacInterface::~MacInterface() {
  ZX_ASSERT_MSG(clients_.is_empty(),
                "Can't dispose MacInterface while clients are still attached (%ld clients left).",
                clients_.size_slow());
}

zx_status_t MacInterface::Create(ddk::MacAddrImplProtocolClient parent,
                                 std::unique_ptr<MacInterface>* out) {
  fbl::AllocChecker ac;
  std::unique_ptr<MacInterface> mac(new (&ac) MacInterface(parent));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  mac->impl_.GetFeatures(&mac->features_);
  if (mac->features_.supported_modes & MODE_MULTICAST_FILTER) {
    mac->default_mode_ = MODE_MULTICAST_FILTER;
  } else if (mac->features_.supported_modes & MODE_MULTICAST_PROMISCUOUS) {
    mac->default_mode_ = MODE_MULTICAST_PROMISCUOUS;
  } else if (mac->features_.supported_modes & MODE_PROMISCUOUS) {
    mac->default_mode_ = MODE_PROMISCUOUS;
  } else {
    LOG_ERROR("mac-addr-device:Init: Invalid device features");
    return ZX_ERR_NOT_SUPPORTED;
  }
  // Limit multicast filter count to protocol definition.
  if (mac->features_.multicast_filter_count > MAX_MAC_FILTER) {
    mac->features_.multicast_filter_count = MAX_MAC_FILTER;
  }
  if ((mac->features_.supported_modes & ~kSupportedModesMask) != 0) {
    LOGF_ERROR("mac-addr-device:Init: Invalid supported modes bitmask: %08X",
               mac->features_.supported_modes);
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Set the default mode to the parent on initialization.
  mac->impl_.SetMode(mac->default_mode_, nullptr, 0);

  *out = std::move(mac);
  return ZX_OK;
}

zx_status_t MacInterface::Bind(async_dispatcher_t* dispatcher, zx::channel req) {
  fbl::AutoLock lock(&lock_);
  if (teardown_callback_) {
    // Don't allow new bindings if we're tearing down.
    return ZX_ERR_BAD_STATE;
  }
  fbl::AllocChecker ac;
  std::unique_ptr<MacClientInstance> client_instance(new (&ac)
                                                         MacClientInstance(this, default_mode_));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  zx_status_t status = client_instance->Bind(dispatcher, std::move(req));

  if (status == ZX_OK) {
    clients_.push_back(std::move(client_instance));
    Consolidate();
  }

  return status;
}

mode_t MacInterface::ConvertMode(const netdev::MacFilterMode& mode) {
  mode_t check = 0;
  switch (mode) {
    case netdev::MacFilterMode::PROMISCUOUS:
      check = MODE_PROMISCUOUS;
      break;
    case netdev::MacFilterMode::MULTICAST_PROMISCUOUS:
      check = MODE_MULTICAST_PROMISCUOUS;
      break;
    case netdev::MacFilterMode::MULTICAST_FILTER:
      check = MODE_MULTICAST_FILTER;
      break;
  }
  while (check != (MODE_PROMISCUOUS << 1u)) {
    if ((features_.supported_modes & check) != 0) {
      return check;
    }
    check <<= 1u;
  }
  return 0;
}

void MacInterface::Consolidate() {
  mode_t mode = default_mode_;
  // Gather the most permissive mode that the clients want.
  for (auto& c : clients_) {
    if (c.state().filter_mode > mode) {
      mode = c.state().filter_mode;
    }
  }
  std::array<uint8_t, MAX_MAC_FILTER * MAC_SIZE> addr_buff{};
  size_t addr_count = 0;
  // If selected mode is multicast filter, then collect all the unique addresses.
  if (mode == MODE_MULTICAST_FILTER) {
    std::unordered_set<ClientState::Addr, ClientState::MacHasher> addresses;
    for (const auto& c : clients_) {
      const auto& client_addresses = c.state().addresses;
      addresses.insert(client_addresses.begin(), client_addresses.end());
      if (addresses.size() > features_.multicast_filter_count) {
        // Try to go into multicast_promiscuous mode, if it's supported.
        auto try_mode = ConvertMode(netdev::MacFilterMode::MULTICAST_PROMISCUOUS);
        // If it's not supported (meaning that neither multicast promiscuous nor promiscuous is
        // supported, since ConvertMode will fall back to the more permissive mode), we have no
        // option but to truncate the address list.
        if (try_mode != 0) {
          mode = try_mode;
        } else {
          LOGF_WARN(
              "Mac filter list is full, but more permissive modes are not supported. Multicast Mac "
              "filter list is being truncated to %d entries",
              features_.multicast_filter_count);
        }
        break;
      }
    }
    // If the mode didn't change out of multicast filter, build the multicast list.
    if (mode == MODE_MULTICAST_FILTER) {
      auto addr_ptr = addr_buff.begin();
      for (auto a = addresses.begin();
           a != addresses.end() && addr_count < features_.multicast_filter_count; a++) {
        addr_ptr = std::copy(a->address.octets.begin(), a->address.octets.end(), addr_ptr);
        addr_count++;
      }
    }
  }
  impl_.SetMode(mode, addr_buff.data(), addr_count);
}

void MacInterface::CloseClient(MacClientInstance* client) {
  fit::callback<void()> teardown;
  {
    fbl::AutoLock lock(&lock_);
    clients_.erase(*client);
    Consolidate();
    if (clients_.is_empty() && teardown_callback_) {
      teardown = std::move(teardown_callback_);
    }
  }

  if (teardown) {
    teardown();
  }
}

void MacInterface::Teardown(fit::callback<void()> callback) {
  fbl::AutoLock lock(&lock_);
  // Can't call teardown if already tearing down.
  ZX_ASSERT(!teardown_callback_);
  if (clients_.is_empty()) {
    lock.release();
    callback();
  } else {
    teardown_callback_ = std::move(callback);
    for (auto& client : clients_) {
      client.Unbind();
    }
  }
}

void MacClientInstance::GetUnicastAddress(GetUnicastAddressCompleter::Sync& completer) {
  MacAddress addr{};
  parent_->impl_.GetAddress(addr.octets.data());
  completer.Reply(addr);
}

void MacClientInstance::SetMode(netdev::MacFilterMode mode, SetModeCompleter::Sync& completer) {
  mode_t resolved_mode = parent_->ConvertMode(mode);
  if (resolved_mode != 0) {
    fbl::AutoLock lock(&parent_->lock_);
    state_.filter_mode = resolved_mode;
    parent_->Consolidate();
    completer.Reply(ZX_OK);
  } else {
    completer.Reply(ZX_ERR_NOT_SUPPORTED);
  }
}

void MacClientInstance::AddMulticastAddress(MacAddress address,
                                            AddMulticastAddressCompleter::Sync& completer) {
  if ((address.octets[0] & kMacMulticast) == 0) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
  } else {
    fbl::AutoLock lock(&parent_->lock_);
    if (state_.addresses.size() < MAX_MAC_FILTER) {
      state_.addresses.insert(ClientState::Addr{address});
      parent_->Consolidate();
      completer.Reply(ZX_OK);
    } else {
      completer.Reply(ZX_ERR_NO_RESOURCES);
    }
  }
}

void MacClientInstance::RemoveMulticastAddress(MacAddress address,
                                               RemoveMulticastAddressCompleter::Sync& completer) {
  if ((address.octets[0] & kMacMulticast) == 0) {
    completer.Reply(ZX_ERR_INVALID_ARGS);
  } else {
    fbl::AutoLock lock(&parent_->lock_);
    state_.addresses.erase(ClientState::Addr{address});
    parent_->Consolidate();
    completer.Reply(ZX_OK);
  }
}

MacClientInstance::MacClientInstance(MacInterface* parent, mode_t default_mode)
    : parent_(parent), state_(default_mode) {}

zx_status_t MacClientInstance::Bind(async_dispatcher_t* dispatcher, zx::channel req) {
  auto result =
      fidl::BindServer(dispatcher, std::move(req), this,
                       fidl::OnUnboundFn<MacClientInstance>(
                           [](MacClientInstance* client_instance, fidl::UnbindInfo, zx::channel) {
                             client_instance->parent_->CloseClient(client_instance);
                           }));
  if (result.is_ok()) {
    binding_ = result.take_value();
    return ZX_OK;
  } else {
    return result.error();
  }
}

void MacClientInstance::Unbind() {
  if (binding_.has_value()) {
    binding_->Unbind();
    binding_.reset();
  }
}

}  // namespace internal
}  // namespace network
