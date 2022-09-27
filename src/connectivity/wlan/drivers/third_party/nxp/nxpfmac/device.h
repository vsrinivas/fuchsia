// Copyright (c) 2022 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_H_

#include <fidl/fuchsia.wlan.wlanphyimpl/cpp/driver/wire.h>
#include <lib/async/dispatcher.h>
#include <lib/ddk/device.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

#include <ddktl/device.h>

#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/bus_interface.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/data_plane.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/event_handler.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/ioctl_adapter.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/mlan.h"
#include "src/connectivity/wlan/drivers/third_party/nxp/nxpfmac/wlan_interface.h"

namespace wlan::nxpfmac {

class Device;
struct DeviceContext;
class DeviceInspect;

using DeviceType =
    ::ddk::Device<Device, ddk::Initializable, ddk::Suspendable, ddk::ServiceConnectable>;

class Device : public DeviceType,
               public fdf::WireServer<fuchsia_wlan_wlanphyimpl::WlanphyImpl>,
               public DataPlaneIfc {
 public:
  // State accessors.
  virtual async_dispatcher_t* GetDispatcher() = 0;

  // ::ddk::Device implementation.
  void DdkInit(ddk::InitTxn txn);
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);
  zx_status_t DdkServiceConnect(const char* service_name, fdf::Channel channel);

  // WlanphyImpl interface implementation.
  void GetSupportedMacRoles(fdf::Arena& arena,
                            GetSupportedMacRolesCompleter::Sync& completer) override;
  void CreateIface(CreateIfaceRequestView request, fdf::Arena& arena,
                   CreateIfaceCompleter::Sync& completer) override;
  void DestroyIface(DestroyIfaceRequestView request, fdf::Arena& arena,
                    DestroyIfaceCompleter::Sync& completer) override;
  void SetCountry(SetCountryRequestView request, fdf::Arena& arena,
                  SetCountryCompleter::Sync& completer) override;
  void GetCountry(fdf::Arena& arena, GetCountryCompleter::Sync& completer) override;
  void ClearCountry(fdf::Arena& arena, ClearCountryCompleter::Sync& completer) override;
  void SetPsMode(SetPsModeRequestView request, fdf::Arena& arena,
                 SetPsModeCompleter::Sync& completer) override;
  void GetPsMode(fdf::Arena& arena, GetPsModeCompleter::Sync& completer) override;

  // DataPlaneIfc implementation
  void OnEapolTransmitted(wlan::drivers::components::Frame&& frame, zx_status_t status) override;
  void OnEapolReceived(wlan::drivers::components::Frame&& frame) override;

  // MOAL notifications
  void OnFirmwareInitComplete(zx_status_t status);
  void OnFirmwareShutdownComplete(zx_status_t status);

 protected:
  explicit Device(zx_device_t* parent);

  // Called when DdkInit is called to complete bus initialization.
  virtual zx_status_t Init(mlan_device* mlan_dev, BusInterface** out_bus) = 0;
  // Called to load a firmware file from the location specified by path into a VMO.
  virtual zx_status_t LoadFirmware(const char* path, zx::vmo* out_fw, size_t* out_size) = 0;

  // This will be called when the driver is being shut down, for example during a reboot, power off
  // or suspend. It is NOT called as part of destruction of the device object (because calling
  // virtual methods in destructors is unreliable). The device may be subject to multiple stages of
  // shutdown, it is therefore possible for shutdown to be called multiple times. The device object
  // may also be destructed after this as a result of the driver framework calling release. Take
  // care that multiple shutdowns or a shutdown followed by destruction does not result in double
  // freeing memory or resources. Because this Device class is not Resumable there is no need to
  // worry about coming back from a shutdown state, it's irreversible.
  virtual void Shutdown() = 0;

 private:
  void PerformShutdown();
  zx_status_t InitFirmware(bool* out_is_pending);
  zx_status_t LoadFirmwareData(const char* path, std::vector<uint8_t>* data_out);

  mlan_device mlan_device_ = {};
  void* mlan_adapter_ = nullptr;
  std::optional<ddk::InitTxn> init_txn_;

  BusInterface* bus_ = nullptr;

  std::mutex lock_;

  WlanInterface* client_interface_;
  WlanInterface* ap_interface_;

  DeviceContext* context_ = nullptr;
  EventHandler event_handler_;
  std::unique_ptr<IoctlAdapter> ioctl_adapter_;
  std::unique_ptr<DataPlane> data_plane_;

  // Dispatcher for the FIDL server
  fdf::Dispatcher fidl_dispatcher_;
  sync_completion_t fidl_dispatcher_completion_;

  EventRegistration defer_rx_work_event_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_H_
