// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_NETWORK_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_NETWORK_DEVICE_H_

#include <fuchsia/hardware/network/device/cpp/banjo.h>
#include <lib/stdcompat/span.h>
#include <zircon/compiler.h>

#include <mutex>
#include <vector>

#include <ddktl/device.h>
#include <wlan/drivers/components/frame.h>
#include <wlan/drivers/components/frame_container.h>
#include <wlan/drivers/components/internal/async_txn.h>

namespace wlan::drivers::components {

// This class is intended to make it easier to construct and work with a
// fuchsia.hardware.network.device device (called netdev here) and its interfaces. The user should
// instantiate an object of this class and provide an implementation of NetworkDevice::Callbacks to
// handle the various calls that are made to it.
//
// The actual network device is created when Init() is called. As part of the creation of the
// device Callbacks::NetDevInit will be called and is a suitable place to perform any setup that
// could fail and prevent the creation of the device by returning an error code. When the device
// is destroyed Callbacks::NetDevRelease will be called which should be used to perform any cleanup.
class NetworkDevice final : public ::ddk::NetworkDeviceImplProtocol<NetworkDevice> {
 public:
  class Callbacks {
   public:
    // Takes zx_status_t as parameter, call txn.Reply(status) to complete transaction.
    using StartTxn = AsyncTxn<zx_status_t>;
    // Does not take a parameter, call txn.Reply() to complete transaction.
    using StopTxn = AsyncTxn<>;

    virtual ~Callbacks();

    // Called when the underlying device is released.
    virtual void NetDevRelease() = 0;

    // Called as part of the initialization of the device. Returning anything but ZX_OK from this
    // will prevent the device from being created.
    virtual zx_status_t NetDevInit() = 0;

    // Start the device's data path. The data path is considered started after txn.Reply() has been
    // called with a ZX_OK status as parameter. The device can call txn.Reply() at any time, either
    // during the invocation of NetDevStart or at a later time from the same thread or another
    // thread. To indicate that the data path has already started, call txn.Reply() with
    // ZX_ERR_ALREADY_BOUND, any other error code indicates a failure to start the data path.
    virtual void NetDevStart(StartTxn txn) = 0;

    // Stop the device's data path. The data path will be considered stopped after txn.Reply() has
    // been called. The device can call txn.Reply() at any time, either during the invocation of
    // NetDevStop or at a later time from the same thread or another thread.
    //
    // When the device receives this call it must return all TX and RX frames previously provided
    // and any further TX and RX frames received while in a stopped state must be immediately
    // returned. TX frames should be returned using NetworkDevice::CompleteTx with status
    // ZX_ERR_UNAVAILABLE and RX frames should be returned using NetworkDevice::CompleteRx with a
    // size of zero.
    virtual void NetDevStop(StopTxn txn) = 0;

    // Get information from the device about the underlying device. This includes details such as RX
    // depth, TX depths, features supported any many others. See the device_info_t struct for more
    // information.
    virtual void NetDevGetInfo(device_info_t* out_info) = 0;

    // Enqueue frames for transmission. A span of frames is provided which represent all the frames
    // to be sent. These frames point to the payload to be transmitted and will have any additional
    // headroom and tailspace specified in device_info_t available. So in order to populate headers
    // for example the driver must first call GrowHead on a frame to place the data pointer at the
    // location where the header should be, then populate the header. Note that the lifetime of the
    // data pointed to by the span is limited to this method call. Once this method implementation
    // returns, the frame objects (but not the underlying data) will be lost. The driver therefore
    // needs to make a copy of these frame objects, for example by placing them in a queue or
    // submitting them to hardware before the method returns.
    virtual void NetDevQueueTx(cpp20::span<Frame> frames) = 0;

    // Enqueue available space to the device, for receiving data into. The device will provide any
    // number of buffers up to a total maximum specified during the NetDevGetInfo call for RX depth.
    // As the device completes RX buffers, thereby giving them back to NetworkDevice, the device
    // will gradually pass them back to the device through this call. The device is expected to
    // store all these space buffers and then use them to receive data. FrameStorage is intended for
    // such usage. As a convenience an array containing the virtual addresses where each VMO is
    // mapped is provided. The VMO id in the buffers can be used as a direct index into this array
    // to obtain the virtual address where the VMO has been mapped into memory. Note that in order
    // to get the address for a specific buffer the offset in the buffer needs to be added to the
    // VMO address. The number of addresses in this array matches the maximum number of possible
    // VMOs, defined by MAX_VMOS.
    virtual void NetDevQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count,
                                    uint8_t* vmo_addrs[]) = 0;

    // Inform the device that a new VMO is being used for data transfer. Each frame simply points to
    // a VMO provided through this call. Usually this VMO is shared among multiple frames and each
    // frame has an offset into the VMO indicating where its data is located. The device may need to
    // perform additional operations at the bus level to make sure that these VMOs are ready for
    // use with the bus, examples of this include making the VMO available for DMA operations. In
    // addition to providing the VMO the method call also provides a location in virtual memory
    // where the VMO has been memory mapped and the size of the mapping.
    virtual zx_status_t NetDevPrepareVmo(uint8_t vmo_id, zx::vmo vmo, uint8_t* mapped_address,
                                         size_t mapped_size) = 0;

    // Inform the device that a VMO will no longer be used for data transfers. The device may have
    // to ensure that any underlying bus is made aware of this to complete the release. The vmo_id
    // is one of the IDs previously supplied in the prepare call. It is guaranteed that this will
    // not be called until the device has returned all TX and RX frames through CompleteTx and
    // CompleteRx. This means that the device does not need to attempt any cleanup and return of
    // frames as a result of this call.
    virtual void NetDevReleaseVmo(uint8_t vmo_id) = 0;

    // Start or stop snooping. This currently has limited support.
    virtual void NetDevSetSnoopEnabled(bool snoop) = 0;
  };

  NetworkDevice(zx_device_t* parent, Callbacks* callbacks);
  virtual ~NetworkDevice();
  NetworkDevice(const NetworkDevice&) = delete;
  NetworkDevice& operator=(const NetworkDevice&) = delete;

  // Initialize the NetworkDevice, this should be called by the device driver when it's ready for
  // the NetworkDevice to start. The device will show up as deviceName in the device tree.
  zx_status_t Init(const char* deviceName);

  // Remove the NetworkDevice, this calls DdkRemove. The removal is not complete until NetDevRelease
  // is called on the Callbacks interface.
  void Remove();

  // This is called by the DDK and the device does not need to call this. The device will be
  // notified of this through the Callbacks interface instead.
  void Release();

  network_device_ifc_protocol_t NetDevIfcProto() const;

  // Notify NetworkDevice of a single incoming RX frame. This method exists for convenience but the
  // driver should prefer to complete as many frames as possible in one call instead of making
  // multiple calls to this method. It is safe to complete a frame with a size of zero, such a frame
  // will be considered unfulfilled. An unfulfilled frame will not be passed up through the network
  // stack and its receive space can be made available to the device again. After this call the
  // device must not expect to be able to use the frame again.
  void CompleteRx(Frame&& frame);
  // Notify NetworkDevice of multiple incoming RX frames. This is the preferred way of receiving
  // frames as receiving multiple frames in one call is more efficient. It is safe to complete
  // frames with a size of zero, such frames will be considered unfulfilled. Unfulfilled frames will
  // not be passed up through the network stack and its receive space can be made available to the
  // device again. This allows the driver to indicate that individual frames in a FrameContainer do
  // not need to be received without having to remove them from the FrameContainer. After this call
  // the device must not expect to be able to use the frames again.
  void CompleteRx(FrameContainer&& frames);
  // Notify NetworkDevice that TX frames have been completed. This can be either because they were
  // successfully received, indicated by a ZX_OK status, or failed somehow, indicated by anything
  // other than ZX_OK. All frames in the sequence will be completed with the same status, if the
  // driver has partially completed a transmission where some frames succeeded and some failed, the
  // driver will have to create separate spans and make multiple calls to this method. After this
  // call the device must not expect to be able to use the frames again.
  void CompleteTx(cpp20::span<Frame> frames, zx_status_t status);

  // NetworkDeviceImpl implementation
  zx_status_t NetworkDeviceImplInit(const network_device_ifc_protocol_t* iface);
  void NetworkDeviceImplStart(network_device_impl_start_callback callback, void* cookie)
      __TA_EXCLUDES(started_mutex_);
  void NetworkDeviceImplStop(network_device_impl_stop_callback callback, void* cookie)
      __TA_EXCLUDES(started_mutex_);
  void NetworkDeviceImplGetInfo(device_info_t* out_info);
  void NetworkDeviceImplQueueTx(const tx_buffer_t* buffers_list, size_t buffers_count)
      __TA_EXCLUDES(started_mutex_);
  void NetworkDeviceImplQueueRxSpace(const rx_space_buffer_t* buffers_list, size_t buffers_count);
  void NetworkDeviceImplPrepareVmo(uint8_t id, zx::vmo vmo,
                                   network_device_impl_prepare_vmo_callback callback, void* cookie);
  void NetworkDeviceImplReleaseVmo(uint8_t id);
  void NetworkDeviceImplSetSnoop(bool snoop);

 private:
  zx_status_t AddNetworkDevice(const char* deviceName);
  bool ShouldCompleteFrame(const Frame& frame);

  zx_device_t* parent_ = nullptr;
  zx_device_t* device_ = nullptr;
  Callbacks* callbacks_;
  bool started_ __TA_GUARDED(started_mutex_) = false;
  std::mutex started_mutex_;
  network_device_impl_protocol_t netdev_proto_;
  ::ddk::NetworkDeviceIfcProtocolClient netdev_ifc_;
  uint8_t* vmo_addrs_[MAX_VMOS] = {};
  uint64_t vmo_lengths_[MAX_VMOS] = {};
  std::vector<Frame> tx_frames_;
};

}  // namespace wlan::drivers::components

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_COMPONENTS_CPP_INCLUDE_WLAN_DRIVERS_COMPONENTS_NETWORK_DEVICE_H_
