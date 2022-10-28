// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "device_dfv2.h"

#include <fidl/fuchsia.wlan.device/cpp/wire.h>
#include <fuchsia/hardware/wlanphyimpl/c/banjo.h>
#include <fuchsia/wlan/common/cpp/fidl.h>
#include <fuchsia/wlan/internal/cpp/fidl.h>
#include <lib/ddk/device.h>
#include <lib/fidl/cpp/wire/arena.h>
#include <net/ethernet.h>
#include <zircon/status.h>

#include <iterator>

#include <wlan/common/band.h>
#include <wlan/common/channel.h>
#include <wlan/common/element.h>
#include <wlan/common/phy.h>

#include "debug.h"
#include "driver.h"

namespace wlanphy {

namespace wlan_common = ::fuchsia::wlan::common;
namespace wlan_internal = ::fuchsia::wlan::internal;

Device::Device(zx_device_t* parent, fdf::ClientEnd<fuchsia_wlan_wlanphyimpl::WlanphyImpl> client)
    : ::ddk::Device<Device, ::ddk::Messageable<fuchsia_wlan_device::Connector>::Mixin,
                    ::ddk::Unbindable>(parent),
      server_dispatcher_(wlanphy_async_t()) {
  ltrace_fn();
  ZX_ASSERT_MSG(parent != nullptr, "No parent device assigned for wlanphy device.");

  auto client_dispatcher = fdf::Dispatcher::Create(0, "wlanphy", [&](fdf_dispatcher_t*) {
    if (unbind_txn_)
      unbind_txn_->Reply();
  });

  ZX_ASSERT_MSG(!client_dispatcher.is_error(), "Creating dispatcher error: %s",
                zx_status_get_string(client_dispatcher.status_value()));

  client_dispatcher_ = std::move(*client_dispatcher);

  client_ = fdf::WireSharedClient<fuchsia_wlan_wlanphyimpl::WlanphyImpl>(std::move(client),
                                                                         client_dispatcher_.get());
}

Device::~Device() { ltrace_fn(); }

zx_status_t Device::DeviceAdd() {
  zx_status_t status;

  if ((status = DdkAdd(::ddk::DeviceAddArgs("wlanphy").set_proto_id(ZX_PROTOCOL_WLANPHY))) !=
      ZX_OK) {
    lerror("failed adding wlanphy device add: %s", zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

void Device::Connect(ConnectRequestView request, ConnectCompleter::Sync& completer) {
  Connect(std::move(request->request));
}

void Device::Connect(fidl::ServerEnd<fuchsia_wlan_device::Phy> server_end) {
  ltrace_fn();
  fidl::BindServer<fidl::WireServer<fuchsia_wlan_device::Phy>>(server_dispatcher_,
                                                               std::move(server_end), this);
}

zx_status_t Device::ConnectToWlanphyImpl(fdf::Channel server_channel) {
  zx_status_t status = ZX_OK;
  status = DdkServiceConnect(fidl::DiscoverableProtocolName<fuchsia_wlan_wlanphyimpl::WlanphyImpl>,
                             std::move(server_channel));
  if (status != ZX_OK) {
    lerror("DdkServiceConnect to wlanphyimpl device Failed =: %s", zx_status_get_string(status));
    return status;
  }
  return status;
}

// Implement DdkRelease for satisfying Ddk's requirement, but this function is not called now.
// Though this device inherites Ddk:Device, DdkAdd() is not called, device_add() is called instead.
void Device::DdkRelease() {
  ltrace_fn();
  delete this;
}

void Device::DdkUnbind(::ddk::UnbindTxn txn) {
  ltrace_fn();
  // Saving the input UnbindTxn to the device, ::ddk::UnbindTxn::Reply() will be called with this
  // UnbindTxn in the shutdown callback of the dispatcher, so that we can make sure DdkUnbind()
  // won't end before the dispatcher shutdown.
  unbind_txn_ = std::move(txn);
  wlanphy_destroy_loop();
  client_dispatcher_.ShutdownAsync();
}

void Device::GetSupportedMacRoles(GetSupportedMacRolesCompleter::Sync& completer) {
  ltrace_fn();
  constexpr uint32_t kTag = 'GSMC';
  fdf::Arena fdf_arena(kTag);

  client_.buffer(fdf_arena)->GetSupportedMacRoles().ThenExactlyOnce(
      [completer = completer.ToAsync()](
          fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::GetSupportedMacRoles>&
              result) mutable {
        if (!result.ok()) {
          completer.ReplyError(result.status());
          return;
        }
        if (result->is_error()) {
          completer.ReplyError(result->error_value());
          return;
        }

        if (result->value()->supported_mac_roles.count() >
            fuchsia_wlan_common_MAX_SUPPORTED_MAC_ROLES) {
          completer.ReplyError(ZX_ERR_OUT_OF_RANGE);
          return;
        }

        completer.ReplySuccess(result->value()->supported_mac_roles);
      });
}

const fidl::Array<uint8_t, 6> NULL_MAC_ADDR{0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

void Device::CreateIface(CreateIfaceRequestView request, CreateIfaceCompleter::Sync& completer) {
  ltrace_fn();
  constexpr uint32_t kTag = 'CIFC';
  fdf::Arena fdf_arena(kTag);

  fidl::Arena fidl_arena;
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplCreateIfaceRequest::Builder(fidl_arena);
  builder.role(request->req.role);
  builder.mlme_channel(std::move(request->req.mlme_channel));

  if (!std::equal(std::begin(NULL_MAC_ADDR), std::end(NULL_MAC_ADDR),
                  request->req.init_sta_addr.data())) {
    builder.init_sta_addr(request->req.init_sta_addr);
  }

  client_.buffer(fdf_arena)
      ->CreateIface(builder.Build())
      .ThenExactlyOnce(
          [completer = completer.ToAsync()](
              fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::CreateIface>&
                  result) mutable {
            if (!result.ok()) {
              lerror("CreateIface failed with FIDL error %s", result.status_string());
              completer.ReplyError(result.status());
              return;
            }
            if (result->is_error()) {
              lerror("CreateIface failed with error %s",
                     zx_status_get_string(result->error_value()));
              completer.ReplyError(result->error_value());
              return;
            }

            if (!result->value()->has_iface_id()) {
              lerror("iface_id does not exist");
              completer.ReplyError(ZX_ERR_INTERNAL);
              return;
            }
            completer.ReplySuccess(result->value()->iface_id());
          });
}

void Device::DestroyIface(DestroyIfaceRequestView request, DestroyIfaceCompleter::Sync& completer) {
  ltrace_fn();
  constexpr uint32_t kTag = 'DIFC';
  fdf::Arena fdf_arena(kTag);

  fidl::Arena fidl_arena;
  auto builder =
      fuchsia_wlan_wlanphyimpl::wire::WlanphyImplDestroyIfaceRequest::Builder(fidl_arena);
  builder.iface_id(request->req.id);

  client_.buffer(fdf_arena)
      ->DestroyIface(builder.Build())
      .ThenExactlyOnce(
          [completer = completer.ToAsync()](
              fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::DestroyIface>&
                  result) mutable {
            if (!result.ok()) {
              lerror("DestroyIface failed with FIDL error %s", result.status_string());
              completer.ReplyError(result.status());
              return;
            }
            if (result->is_error()) {
              lerror("DestroyIface failed with error %s",
                     zx_status_get_string(result->error_value()));
              completer.ReplyError(result->error_value());
              return;
            }

            completer.ReplySuccess();
          });
}

void Device::SetCountry(SetCountryRequestView request, SetCountryCompleter::Sync& completer) {
  ltrace_fn();
  ldebug_device("SetCountry to %s", wlan::common::Alpha2ToStr(request->req.alpha2).c_str());
  constexpr uint32_t kTag = 'SCNT';
  fdf::Arena fdf_arena(kTag);

  auto alpha2 = ::fidl::Array<uint8_t, WLANPHY_ALPHA2_LEN>();
  memcpy(alpha2.data(), request->req.alpha2.data(), WLANPHY_ALPHA2_LEN);

  auto out_country = fuchsia_wlan_wlanphyimpl::wire::WlanphyCountry::WithAlpha2(alpha2);
  client_.buffer(fdf_arena)
      ->SetCountry(out_country)
      .ThenExactlyOnce(
          [completer = completer.ToAsync()](
              fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::SetCountry>&
                  result) mutable {
            if (!result.ok()) {
              lerror("SetCountry failed with FIDL error %s", result.status_string());
              completer.Reply(result.status());
              return;
            }
            if (result->is_error()) {
              lerror("SetCountry failed with error %s",
                     zx_status_get_string(result->error_value()));
              completer.Reply(result->error_value());
              return;
            }

            completer.Reply(ZX_OK);
          });
}

void Device::GetCountry(GetCountryCompleter::Sync& completer) {
  ltrace_fn();
  constexpr uint32_t kTag = 'GCNT';
  fdf::Arena fdf_arena(kTag);

  client_.buffer(fdf_arena)->GetCountry().ThenExactlyOnce(
      [completer = completer.ToAsync()](
          fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::GetCountry>&
              result) mutable {
        fuchsia_wlan_device::wire::CountryCode resp;
        zx_status_t status;
        if (!result.ok()) {
          ldebug_device("GetCountry failed with FIDL error %s", result.status_string());
          status = result.status();
          completer.ReplyError(status);
          return;
        }
        if (result->is_error()) {
          ldebug_device("GetCountry failed with error %s",
                        zx_status_get_string(result->error_value()));
          status = result->error_value();
          completer.ReplyError(status);
          return;
        }
        if (!result->value()->country.is_alpha2()) {
          lerror("only alpha2 format is supported");
          completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
          return;
        }
        memcpy(resp.alpha2.data(), result->value()->country.alpha2().data(), WLANPHY_ALPHA2_LEN);

        completer.ReplySuccess(resp);
      });
}

void Device::ClearCountry(ClearCountryCompleter::Sync& completer) {
  ltrace_fn();
  constexpr uint32_t kTag = 'CCNT';
  fdf::Arena fdf_arena(kTag);

  client_.buffer(fdf_arena)->ClearCountry().ThenExactlyOnce(
      [completer = completer.ToAsync()](
          fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::ClearCountry>&
              result) mutable {
        if (!result.ok()) {
          ldebug_device("ClearCountry failed with FIDL error %s", result.status_string());
          completer.Reply(result.status());
          return;
        }
        if (result->is_error()) {
          ldebug_device("ClearCountry failed with error %s",
                        zx_status_get_string(result->error_value()));
          completer.Reply(result->error_value());
          return;
        }

        completer.Reply(ZX_OK);
      });
}

void Device::SetPsMode(SetPsModeRequestView request, SetPsModeCompleter::Sync& completer) {
  ltrace_fn();
  ldebug_device("SetPsMode to %d", request->req);
  constexpr uint32_t kTag = 'SPSM';
  fdf::Arena fdf_arena(kTag);

  fidl::Arena fidl_arena;
  auto builder = fuchsia_wlan_wlanphyimpl::wire::WlanphyImplSetPsModeRequest::Builder(fidl_arena);
  builder.ps_mode(request->req);

  client_.buffer(fdf_arena)
      ->SetPsMode(builder.Build())
      .ThenExactlyOnce([completer = completer.ToAsync()](
                           fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::SetPsMode>&
                               result) mutable {
        if (!result.ok()) {
          ldebug_device("SetPsMode failed with FIDL error %s", result.status_string());
          completer.Reply(result.status());
          return;
        }
        if (result->is_error()) {
          ldebug_device("SetPsMode failed with error %s",
                        zx_status_get_string(result->error_value()));
          completer.Reply(result->error_value());
          return;
        }

        completer.Reply(ZX_OK);
      });
}

void Device::GetPsMode(GetPsModeCompleter::Sync& completer) {
  ltrace_fn();
  constexpr uint32_t kTag = 'GPSM';
  fdf::Arena fdf_arena(kTag);

  client_.buffer(fdf_arena)->GetPsMode().ThenExactlyOnce(
      [completer = completer.ToAsync()](
          fdf::WireUnownedResult<fuchsia_wlan_wlanphyimpl::WlanphyImpl::GetPsMode>&
              result) mutable {
        if (!result.ok()) {
          ldebug_device("GetPsMode failed with FIDL error %s", result.status_string());
          completer.ReplyError(result.status());
          return;
        }
        if (result->is_error()) {
          ldebug_device("GetPsMode failed with error %s",
                        zx_status_get_string(result->error_value()));
          completer.ReplyError(result->error_value());
          return;
        }

        if (!result->value()->has_ps_mode()) {
          lerror("ps mode is not present in response");
          completer.ReplyError(ZX_ERR_INTERNAL);
          return;
        }

        completer.ReplySuccess(result->value()->ps_mode());
      });
}
}  // namespace wlanphy
