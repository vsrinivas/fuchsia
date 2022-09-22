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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_CONTEXT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_CONTEXT_H_

namespace wlan::nxpfmac {

class DataPlane;
class Device;
class EventHandler;
class IoctlAdapter;

// A struct used as the context in many places in the driver that need to access device specific
// data. Among other things it's used as the context for moal callbacks but it's also used to pass
// around these common pointers that are used in many components without having to pass around
// multiple pointer or needing those components to be aware of the Device class and requiring that
// the Device class expose its internals.
//
// This should be placed in mlan_device->pmoal_handle by the bus level device. The intention is that
// the bus level device will extend this struct with bus specific data if needed and that Device
// will add on device specific data.
//
// Avoid placing interface related information here, this is only intended for things whose lifetime
// is tied to the Device instance. That way other parts of the code don't have to manage interface
// object lifetimes in multiple places.
struct DeviceContext {
  Device* device_;
  EventHandler* event_handler_;
  IoctlAdapter* ioctl_adapter_;
  DataPlane* data_plane_;
};

}  // namespace wlan::nxpfmac

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_NXP_NXPFMAC_DEVICE_CONTEXT_H_
