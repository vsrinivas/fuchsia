// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/sync/completion.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/array.h>

#include "fidl-helper.h"

namespace fake_ddk {

// Generic protocol.
struct Protocol {
  void* ops;
  void* ctx;
};

struct ProtocolEntry {
  uint32_t id;
  Protocol proto;
};

// Fake instances of a parent device, and device returned by DeviceAdd.
extern zx_device_t* kFakeDevice;
extern zx_device_t* kFakeParent;

// Return above instances, after first checking that Bind() instance was initialized.
extern zx_device_t* FakeDevice();
extern zx_device_t* FakeParent();

typedef void(UnbindOp)(void* ctx);

// Mocks the bind/unbind functionality provided by the DDK(TL).
//
// The typical use of this class is something like:
//      fake_ddk::Bind ddk;
//      device->Bind();
//      device->DdkAsyncRemove();
//      EXPECT_TRUE(ddk.Ok());
//
// Note that this class is not thread safe. Only one test at a time is supported.
class Bind {
 public:
  Bind();
  virtual ~Bind() { instance_ = nullptr; }

  // Verifies that the whole process of bind and unbind went as expected.
  bool Ok();

  // Sets optional expectations for DeviceAddMetadata(). If used, the provided
  // pointer must remain valid until the call to DeviceAddMetadata(). If the
  // provided data doesn't match the expectations, DeviceAddMetadata will fail
  // with ZX_ERR_BAD_STATE.
  void ExpectMetadata(const void* data, size_t data_length);

  // Blocking wait until DdkRemove is called. Use this if you expect unbind/remove to
  // be called in a different thread.
  zx_status_t WaitUntilRemove();

  // Returns the number of times DeviceAddMetadata has been called and the
  // total length of all the data provided.
  void GetMetadataInfo(int* num_calls, size_t* length);

  // Sets data returned by DeviceGetMetadata(). If used, the provided
  // pointer must remain valid until the call to DeviceGetMetadata().
  void SetMetadata(const void* data, size_t data_length);

  // Sets an optional list of protocols that the ddk should return for the
  // parent device.
  void SetProtocols(fbl::Array<ProtocolEntry>&& protocols);

  // Sets an optional size that the ddk should return for the parent device.
  void SetSize(zx_off_t size);

  static Bind* Instance() { return instance_; }

  zx::channel& FidlClient() { return fidl_.local(); }

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                zx_device_t** out);

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceRemove(zx_device_t* device);

  // Internal fake implementation of ddk functionality.
  virtual void DeviceAsyncRemove(zx_device_t* device);

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceAddMetadata(zx_device_t* dev, uint32_t type, const void* data,
                                        size_t length);

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* data, size_t length,
                                        size_t* actual);

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size);

  // Internal fake implementation of ddk functionality.
  virtual void DeviceMakeVisible(zx_device_t* dev);

  // Internal fake implementation of ddk functionaility.
  virtual void DeviceSuspendComplete(zx_device_t* device, zx_status_t status, uint8_t out_state);

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                        void* protocol);

  // Internal fake implementation of ddk functionality.
  virtual zx_status_t DeviceRebind(zx_device_t* device);

  // Internal fake implementation of ddk functionality.
  virtual const char* DeviceGetName(zx_device_t* device);

  // Internal fake implementation of ddk functionality.
  virtual zx_off_t DeviceGetSize(zx_device_t* device);

 protected:
  static Bind* instance_;

  bool bad_parent_ = false;
  bool bad_device_ = false;
  bool add_called_ = false;
  bool remove_called_ = false;
  bool rebind_called_ = false;
  sync_completion_t remove_called_sync_;
  bool make_visible_called_ = false;
  bool suspend_complete_called_ = false;

  int add_metadata_calls_ = 0;
  size_t metadata_length_ = 0;
  const void* metadata_ = nullptr;

  int get_metadata_calls_ = 0;
  size_t get_metadata_length_ = 0;
  const void* get_metadata_ = nullptr;

  zx_off_t size_ = 0;

  fbl::Array<ProtocolEntry> protocols_;
  FidlMessenger fidl_;

  UnbindOp* unbind_op_ = nullptr;
  void* op_ctx_ = nullptr;
  bool unbind_called_ = false;
};

}  // namespace fake_ddk
