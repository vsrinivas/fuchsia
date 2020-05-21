// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FAKE_DDK_H_
#define SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FAKE_DDK_H_

#include <lib/sync/completion.h>
#include <lib/syslog/logger.h>

#include <optional>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/fragment-device.h>
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

// The minimum log severity for drivers using the fake DDK.
extern fx_log_severity_t kMinLogSeverity;

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

  // Blocking wait until InitTxn.Reply() is called. Use this if you expect the init
  // reply to be called in a different thread.
  zx_status_t WaitUntilInitComplete();

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

 protected:
  // Internal fake implementations of DDK functionality.
  //
  // The Fake DDK provides default implementations for all of these methods,
  // but they are exposed here to allow tests to override particular function
  // calls in the DDK.
  //
  // For example, calls to the global function "device_add_from_driver"
  // are sent to the active Bind instance's "DeviceAdd" method. A test
  // is able to override the DeviceAdd method call to provide
  // additional/different behaviour for the call.

  virtual zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                                zx_device_t** out);
  virtual void DeviceInitReply(zx_device_t* device, zx_status_t status,
                               const device_init_reply_args_t* args);
  virtual zx_status_t DeviceRemove(zx_device_t* device);
  virtual void DeviceAsyncRemove(zx_device_t* device);
  virtual zx_status_t DeviceAddMetadata(zx_device_t* dev, uint32_t type, const void* data,
                                        size_t length);
  virtual zx_status_t DeviceGetMetadata(zx_device_t* dev, uint32_t type, void* data, size_t length,
                                        size_t* actual);
  virtual zx_status_t DeviceGetMetadataSize(zx_device_t* dev, uint32_t type, size_t* out_size);
  virtual void DeviceMakeVisible(zx_device_t* dev);
  virtual void DeviceSuspendComplete(zx_device_t* device, zx_status_t status, uint8_t out_state);
  virtual void DeviceResumeComplete(zx_device_t* device, zx_status_t status,
                                    uint8_t out_power_state, uint32_t out_perf_state);
  virtual zx_status_t DeviceGetProtocol(const zx_device_t* device, uint32_t proto_id,
                                        void* protocol);
  virtual zx_status_t DeviceOpenProtocolSessionMultibindable(const zx_device_t* dev,
                                                             uint32_t proto_id, void* protocol);
  virtual zx_status_t DeviceRebind(zx_device_t* device);
  virtual const char* DeviceGetName(zx_device_t* device);
  virtual zx_off_t DeviceGetSize(zx_device_t* device);

  // Allow the DDK entry points to access the above class members.
  //
  // We use the syntax "zx_status_t (::foo)()" to avoid clang from
  // misparsing as "foo" as a member function of "zx_status_t"; i.e.,
  // "(zx_status_t::foo)()".
  friend zx_status_t(::device_add_from_driver)(zx_driver_t* drv, zx_device_t* parent,
                                               device_add_args_t* args, zx_device_t** out);

  friend zx_status_t(::device_remove_deprecated)(zx_device_t* device);
  friend void(::device_async_remove)(zx_device_t* device);
  friend void(::device_init_reply)(zx_device_t* device, zx_status_t status,
                                   const device_init_reply_args_t* args);
  friend void(::device_unbind_reply)(zx_device_t* device);
  friend void(::device_suspend_reply)(zx_device_t* dev, zx_status_t status, uint8_t out_state);

  friend void(::device_resume_reply)(zx_device_t* dev, zx_status_t status, uint8_t out_power_state,
                                     uint32_t out_perf_state);
  friend zx_status_t(::device_add_metadata)(zx_device_t* device, uint32_t type, const void* data,
                                            size_t length);
  friend void(::device_make_visible)(zx_device_t* device, const device_make_visible_args_t* args);
  friend zx_status_t(::device_get_protocol)(const zx_device_t* device, uint32_t proto_id,
                                            void* protocol);
  friend zx_status_t(::device_open_protocol_session_multibindable)(const zx_device_t* dev,
                                                                   uint32_t proto_id,
                                                                   void* protocol);
  friend const char*(::device_get_name)(zx_device_t* device);
  friend zx_off_t(::device_get_size)(zx_device_t* device);
  friend zx_status_t(::device_get_metadata)(zx_device_t* device, uint32_t type, void* buf,
                                            size_t buflen, size_t* actual);
  friend zx_status_t(::device_get_metadata_size)(zx_device_t* device, uint32_t type,
                                                 size_t* out_size);
  friend zx_status_t(::device_rebind)(zx_device_t* device);

  // These fields should be private, but are currently referenced extensively
  // by tests inheriting from us.
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
  bool resume_complete_called_ = false;
  bool device_open_protocol_session_multibindable_ = false;

  int add_metadata_calls_ = 0;
  size_t metadata_length_ = 0;
  const void* metadata_ = nullptr;

  int get_metadata_calls_ = 0;
  size_t get_metadata_length_ = 0;
  const void* get_metadata_ = nullptr;

  zx_off_t size_ = 0;

  fbl::Array<ProtocolEntry> protocols_;
  FidlMessenger fidl_;

  bool has_init_hook_ = false;
  std::optional<zx_status_t> init_reply_;
  sync_completion_t init_replied_sync_;

  UnbindOp* unbind_op_ = nullptr;
  void* op_ctx_ = nullptr;
  bool unbind_called_ = false;
};

}  // namespace fake_ddk

#endif  // SRC_DEVICES_TESTING_FAKE_DDK_INCLUDE_LIB_FAKE_DDK_FAKE_DDK_H_
