// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_BUS_H_
#define GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_BUS_H_

#include <ddk/device.h>
#include <zircon/types.h>

#include <memory>

#include "register.h"

namespace wlan {
namespace rtl88xx {

// This interface describes a bus, such as PCIE, USB, or SDIO, over which we can communicate with
// the hardware.
class Bus {
   public:
    enum class BusType {
        kInvalid = 0,
        kUsb = 1,
    };

    virtual ~Bus() = 0;

    // Factory function for Bus instances. Returns an instance iff `bus_device` implements a
    // supported protocol, and the Bus can be constructed on that protocol.
    static zx_status_t Create(zx_device_t* bus_device, std::unique_ptr<Bus>* bus);

    // Return the bus type, for conditional code that needs it.
    virtual BusType GetBusType() const = 0;

    // Convenience functions that dispatch register access to the appropriate register offset and
    // value width.

    // Read the value of a register from the bus.
    template <typename RegisterType> zx_status_t ReadRegister(RegisterType* value) {
        return ReadRegister(value->addr(), value->mut_val(), RegisterType::name());
    }

    // Write the value of a register to the bus.
    template <typename RegisterType> zx_status_t WriteRegister(const RegisterType& value) {
        return WriteRegister(value.addr(), value.val(), RegisterType::name());
    }

    // Register read/write implementation.
    virtual zx_status_t ReadRegister(uint16_t offset, uint8_t* value, const char* name) = 0;
    virtual zx_status_t ReadRegister(uint16_t offset, uint16_t* value, const char* name) = 0;
    virtual zx_status_t ReadRegister(uint16_t offset, uint32_t* value, const char* name) = 0;
    virtual zx_status_t WriteRegister(uint16_t offset, uint8_t value, const char* name) = 0;
    virtual zx_status_t WriteRegister(uint16_t offset, uint16_t value, const char* name) = 0;
    virtual zx_status_t WriteRegister(uint16_t offset, uint32_t value, const char* name) = 0;
};

}  // namespace rtl88xx
}  // namespace wlan

#endif  // GARNET_DRIVERS_WLAN_REALTEK_RTL88XX_BUS_H_
