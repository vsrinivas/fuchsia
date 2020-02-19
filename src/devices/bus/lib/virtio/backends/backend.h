// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_BUS_LIB_VIRTIO_BACKENDS_BACKEND_H_
#define SRC_DEVICES_BUS_LIB_VIRTIO_BACKENDS_BACKEND_H_

#include <lib/device-protocol/pci.h>
#include <lib/zx/handle.h>

#include <ddk/protocol/pci.h>
#include <fbl/mutex.h>
#include <virtio/virtio.h>

// Each backend will implement their own method for initialization / binding
// based on their own internal functionality. Since we will have different
// base drivers at a bind level to bind to pci vs mmio, that layer will
// be able to determine what needs to be called and what parameters to pass.
//
// ex: A device bound as a pci device will know to create a PCI backend
// with the protocol and device info parameters.
namespace virtio {

class Backend {
 public:
  Backend() {}
  virtual ~Backend() { irq_handle_.reset(); }
  virtual zx_status_t Bind() = 0;
  virtual void Unbind() {}

  // Returns true if the specified feature bit is set
  virtual bool ReadFeature(uint32_t bit) = 0;
  // Does a Driver -> Device acknowledgement of a feature bit
  virtual void SetFeature(uint32_t bit) = 0;
  // Does a FEATURES_OK check
  virtual zx_status_t ConfirmFeatures() = 0;
  // Device lifecycle methods
  virtual void DriverStatusOk() = 0;
  virtual void DriverStatusAck() = 0;
  virtual void DeviceReset() = 0;

  //// Read/Write the device config
  virtual void ReadDeviceConfig(uint16_t offset, uint8_t* value) = 0;
  virtual void ReadDeviceConfig(uint16_t offset, uint16_t* value) = 0;
  virtual void ReadDeviceConfig(uint16_t offset, uint32_t* value) = 0;
  virtual void ReadDeviceConfig(uint16_t offset, uint64_t* value) = 0;
  virtual void WriteDeviceConfig(uint16_t offset, uint8_t value) = 0;
  virtual void WriteDeviceConfig(uint16_t offset, uint16_t value) = 0;
  virtual void WriteDeviceConfig(uint16_t offset, uint32_t value) = 0;
  virtual void WriteDeviceConfig(uint16_t offset, uint64_t value) = 0;

  // Ring methods vary based on backend due to config offsets and field sizes.
  virtual uint16_t GetRingSize(uint16_t index) = 0;
  virtual void SetRing(uint16_t index, uint16_t count, zx_paddr_t pa_desc, zx_paddr_t pa_avail,
                       zx_paddr_t pa_used) = 0;
  virtual void RingKick(uint16_t ring_index) = 0;
  // Expected to read the interrupt status out of the config based on the offset/address
  // specified by the isr capability.
  virtual uint32_t IsrStatus() = 0;
  virtual zx_status_t InterruptValid() = 0;
  // Wait for the device to raise an interrupt; may return early or may time out after an
  // internal waiting period.
  // Returns ZX_OK if woken by an interrupt
  //         ZX_ERR_TIMED_OUT if an internal timeout expired; there may be work from the device
  virtual zx_status_t WaitForInterrupt() = 0;
  virtual void InterruptAck() = 0;

  DISALLOW_COPY_ASSIGN_AND_MOVE(Backend);

 protected:
  // For protecting irq access / status
  zx::handle irq_handle_;
};

}  // namespace virtio

#endif  // SRC_DEVICES_BUS_LIB_VIRTIO_BACKENDS_BACKEND_H_
