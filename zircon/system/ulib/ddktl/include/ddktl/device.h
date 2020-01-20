// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_DEVICE_H_
#define DDKTL_DEVICE_H_

#include <zircon/assert.h>

#include <type_traits>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <ddktl/init-txn.h>
#include <ddktl/suspend-txn.h>
#include <ddktl/unbind-txn.h>

// ddk::Device<D, ...>
//
// Notes:
//
// ddk::Device<D, ...> is a mixin class that simplifies writing DDK drivers in
// C++. The DDK's zx_device_t defines a set of function pointer callbacks that
// can be implemented to define standard behavior (e.g., open/close/read/write),
// as well as to implement device lifecycle events (e.g., unbind/release). The
// mixin classes are used to set up the function pointer table to call methods
// from the user's class automatically.
//
// Every ddk::Device subclass must implement the following release callback to
// cleanup resources:
//
// void DdkRelease();
//
//
// :: Available mixins ::
// +-------------------------+----------------------------------------------------+
// | Mixin class             | Required function implementation                   |
// +-------------------------+----------------------------------------------------+
// | ddk::GetProtocolable    | zx_status_t DdkGetProtocol(uint32_t proto_id,      |
// |                         |                            void* out)              |
// |                         |                                                    |
// | ddk::Initializable      | void DdkInit(ddk::InitTxn txn)                     |
// |                         |                                                    |
// | ddk::Openable           | zx_status_t DdkOpen(zx_device_t** dev_out,         |
// |                         |                     uint32_t flags)                |
// |                         |                                                    |
// | ddk::Closable           | zx_status_t DdkClose(uint32_t flags)               |
// |                         |                                                    |
// | ddk::UnbindableNew      | void DdkUnbindNew(ddk::UnbindTxn txn)              |
// |                         |                                                    |
// | ddk::PerformanceTunable | zx_status_t DdkSetPerformanceState(                |
// |                         |                           uint32_t requested_state,|
// |                         |                           uint32_t* out_state)     |
// |                         |                                                    |
// | ddk::AutoSuspendable    | zx_status_t DdkConfigureAutoSuspend(bool enable,   |
// |                         |                      uint8_t requested_sleep_state)|
// |                         |                                                    |
// | ddk::Messageable        | zx_status_t DdkMessage(fidl_msg_t* msg,            |
// |                         |                        fidl_txn_t* txn)            |
// |                         |                                                    |
// | ddk::SuspendableNew     | void DdkSuspendNew(ddk::SuspendTxn txn)            |
// |                         |                                                    |
// | ddk::ResumableNew       | zx_status_t DdkResumeNew(uint8_t requested_state,  |
// |                         |                          uint8_t* out_state)       |
// |                         |                                                    |
// | ddk::Rxrpcable          | zx_status_t DdkRxrpc(zx_handle_t channel)          |
// +-------------------------+----------------------------------------------------+
//
// Deprecated Mixins:
// +--------------------------+----------------------------------------------------+
// | Mixin class              | Required function implementation                   |
// +--------------------------+----------------------------------------------------+
// | ddk::Readable            | zx_status_t DdkRead(void* buf, size_t count,       |
// |                          |                     zx_off_t off, size_t* actual)  |
// |                          |                                                    |
// | ddk::Writable            | zx_status_t DdkWrite(const void* buf,              |
// |                          |                      size_t count, zx_off_t off,   |
// |                          |                      size_t* actual)               |
// |                          |                                                    |
// | ddk::GetSizable          | zx_off_t DdkGetSize()                              |
// |                          |                                                    |
// | ddk::Resumable           | zx_status_t DdkResume(uint32_t flags)              |
// |                          |                                                    |
// | ddk::UnbindableDeprecated| void DdkUnbindDeprecated()                         |
// |                          |                                                    |
// +--------------------------+----------------------------------------------------+
//
//
// Note: the ddk::FullDevice type alias may also be used if your device class
// will implement every mixin.
//
//
// :: Example ::
//
// // Define our device type using a type alias.
// class MyDevice;
// using DeviceType = ddk::Device<MyDevice, ddk::Openable, ddk::Closable,
//                                          ddk::Readable, ddk::Unbindable, ddk::SuspendableNew>;
//
// class MyDevice : public DeviceType {
//   public:
//     MyDevice(zx_device_t* parent)
//       : DeviceType(parent) {}
//
//     zx_status_t Bind() {
//         // Any other setup required by MyDevice. The device_add_args_t will be filled out by the
//         // base class.
//         return DdkAdd("my-device-name");
//     }
//
//     // Methods required by the ddk mixins
//     zx_status_t DdkOpen(zx_device_t** dev_out, uint32_t flags);
//     zx_status_t DdkClose(uint32_t flags);
//     zx_status_t DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual);
//     void DdkUnbindNew(ddk::UnbindTxn txn);
//     void DdkSuspendNew(ddk::SuspendTxn txn);
//     void DdkRelease();
// };
//
// extern "C" zx_status_t my_bind(zx_device_t* device,
//                                void** cookie) {
//     auto dev = make_unique<MyDevice>(device);
//     auto status = dev->Bind();
//     if (status == ZX_OK) {
//         // devmgr is now in charge of the memory for dev
//         dev.release();
//     }
//     return status;
// }
//
// See also: protocol mixins for setting protocol_id and protocol_ops.

namespace ddk {

struct AnyProtocol {
  void* ops;
  void* ctx;
};

// base_mixin is a tag that all mixins must inherit from.
using base_mixin = internal::base_mixin;

// base_protocol is a tag used by protocol implementations
using base_protocol = internal::base_protocol;

// DDK Device mixins

template <typename D>
class GetProtocolable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckGetProtocolable<D>();
    proto->get_protocol = GetProtocol;
  }

 private:
  static zx_status_t GetProtocol(void* ctx, uint32_t proto_id, void* out) {
    return static_cast<D*>(ctx)->DdkGetProtocol(proto_id, out);
  }
};

template <typename D>
class Initializable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckInitializable<D>();
    proto->init = Init;
  }

 private:
  static void Init(void* ctx) {
    auto dev = static_cast<D*>(ctx);
    InitTxn txn(dev->zxdev());
    dev->DdkInit(std::move(txn));
  }
};

template <typename D>
class Openable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckOpenable<D>();
    proto->open = Open;
  }

 private:
  static zx_status_t Open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    return static_cast<D*>(ctx)->DdkOpen(dev_out, flags);
  }
};

template <typename D>
class Closable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckClosable<D>();
    proto->close = Close;
  }

 private:
  static zx_status_t Close(void* ctx, uint32_t flags) {
    return static_cast<D*>(ctx)->DdkClose(flags);
  }
};

template <typename D>
class UnbindableDeprecated : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckUnbindableDeprecated<D>();
    proto->unbind = Unbind_Deprecated;
  }

 private:
  static void Unbind_Deprecated(void* ctx) { static_cast<D*>(ctx)->DdkUnbindDeprecated(); }
};

template <typename D>
class UnbindableNew : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckUnbindableNew<D>();
    proto->unbind = Unbind_New;
  }

 private:
  static void Unbind_New(void* ctx) {
    auto dev = static_cast<D*>(ctx);
    UnbindTxn txn(dev->zxdev());
    dev->DdkUnbindNew(std::move(txn));
  }
};

template <typename D>
class Readable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckReadable<D>();
    proto->read = Read;
  }

 private:
  static zx_status_t Read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    return static_cast<D*>(ctx)->DdkRead(buf, count, off, actual);
  }
};

template <typename D>
class Writable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckWritable<D>();
    proto->write = Write;
  }

 private:
  static zx_status_t Write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
    return static_cast<D*>(ctx)->DdkWrite(buf, count, off, actual);
  }
};

template <typename D>
class GetSizable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckGetSizable<D>();
    proto->get_size = GetSize;
  }

 private:
  static zx_off_t GetSize(void* ctx) { return static_cast<D*>(ctx)->DdkGetSize(); }
};

template <typename D>
class Messageable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckMessageable<D>();
    proto->message = Message;
  }

 private:
  static zx_status_t Message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
    return static_cast<D*>(ctx)->DdkMessage(msg, txn);
  }
};

template <typename D>
class SuspendableNew : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckSuspendableNew<D>();
    proto->suspend_new = Suspend_New;
  }

 private:
  static void Suspend_New(void* ctx, uint8_t requested_state, bool enable_wake,
                          uint8_t suspend_reason) {
    auto dev = static_cast<D*>(ctx);
    SuspendTxn txn(dev->zxdev(), requested_state, enable_wake, suspend_reason);
    static_cast<D*>(ctx)->DdkSuspendNew(std::move(txn));
  }
};

template <typename D>
class PerformanceTunable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckPerformanceTunable<D>();
    proto->set_performance_state = set_performance_state;
  }

 private:
  static zx_status_t set_performance_state(void* ctx, uint32_t requested_state,
                                           uint32_t* out_state) {
    return static_cast<D*>(ctx)->DdkSetPerformanceState(requested_state, out_state);
  }
};

template <typename D>
class AutoSuspendable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckConfigureAutoSuspend<D>();
    proto->configure_auto_suspend = Configure_Auto_Suspend;
  }

 private:
  static zx_status_t Configure_Auto_Suspend(void* ctx, bool enable, uint8_t requested_sleep_state) {
    return static_cast<D*>(ctx)->DdkConfigureAutoSuspend(enable, requested_sleep_state);
  }
};

template <typename D>
class ResumableNew : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckResumableNew<D>();
    proto->resume_new = Resume_New;
  }

 private:
  static zx_status_t Resume_New(void* ctx, uint8_t requested_state, uint8_t* out_state) {
    return static_cast<D*>(ctx)->DdkResumeNew(requested_state, out_state);
  }
};

template <typename D>
class Resumable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckResumable<D>();
    proto->resume = Resume;
  }

 private:
  static zx_status_t Resume(void* ctx, uint32_t flags) {
    return static_cast<D*>(ctx)->DdkResume(flags);
  }
};

template <typename D>
class Rxrpcable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckRxrpcable<D>();
    proto->rxrpc = Rxrpc;
  }

 private:
  static zx_status_t Rxrpc(void* ctx, zx_handle_t channel) {
    return static_cast<D*>(ctx)->DdkRxrpc(channel);
  }
};

template <typename D>
class ChildPreReleaseable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckChildPreReleaseable<D>();
    proto->child_pre_release = ChildPreRelease;
  }

 private:
  static void ChildPreRelease(void* ctx, void* child_ctx) {
    static_cast<D*>(ctx)->DdkChildPreRelease(child_ctx);
  }
};

// Device is templated on the list of mixins that define which DDK device
// methods are implemented. Note that internal::base_device *must* be the
// left-most base class in order to ensure that its constructor runs before the
// mixin constructors. This ensures that ddk_device_proto_ is zero-initialized
// before setting the fields in the mixins.
template <class D, template <typename> class... Mixins>
class Device : public ::ddk::internal::base_device<D, Mixins...> {
 public:
  zx_status_t DdkAdd(const char* name, uint32_t flags = 0, zx_device_prop_t* props = nullptr,
                     uint32_t prop_count = 0, uint32_t proto_id = 0,
                     const char* proxy_args = nullptr,
                     zx_handle_t client_remote = ZX_HANDLE_INVALID,
                     const device_power_state_info_t* power_states = nullptr,
                     const uint8_t power_state_count = 0,
                     const device_performance_state_info_t* perf_power_states = nullptr,
                     const uint8_t perf_power_state_count = 0) {
    if (this->zxdev_ != nullptr) {
      return ZX_ERR_BAD_STATE;
    }

    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    // Since we are stashing this as a D*, we can use ctx in all
    // the callback functions and cast it directly to a D*.
    args.ctx = static_cast<D*>(this);
    args.ops = &this->ddk_device_proto_;
    args.flags = flags;
    args.props = props;
    args.prop_count = prop_count;
    args.proto_id = proto_id;
    args.proxy_args = proxy_args;
    args.client_remote = client_remote;
    args.power_states = power_states;
    args.power_state_count = power_state_count;
    args.performance_states = perf_power_states;
    args.performance_state_count = perf_power_state_count;
    AddProtocol(&args);

    return device_add(this->parent_, &args, &this->zxdev_);
  }

  zx_status_t DdkAddComposite(const char* name, const composite_device_desc_t* comp_desc) {
    return device_add_composite(this->parent_, name, comp_desc);
  }

  void DdkMakeVisible(const device_power_state_info_t* power_states = nullptr,
                      const uint8_t power_state_count = 0,
                      const device_performance_state_info_t* perf_power_states = nullptr,
                      const uint8_t perf_power_state_count = 0) {
    device_make_visible_args_t args = {};
    args.power_states = power_states;
    args.power_state_count = power_state_count;
    args.performance_states = perf_power_states;
    args.performance_state_count = perf_power_state_count;
    device_make_visible(zxdev(), &args);
  }

  // Removes the device.
  // This method may have the side-effect of destroying this object if the
  // device's reference count drops to zero.
  //
  // DEPRECATED (fxb/34574).
  // To schedule removal of a device, use DdkAsyncRemove() instead.
  // To signal completion of the device's DdkUnbind(txn) hook, use txn.Reply() instead.
  zx_status_t DdkRemoveDeprecated() {
    if (this->zxdev_ == nullptr) {
      return ZX_ERR_BAD_STATE;
    }

    // The call to |device_remove| must be last since it decrements the
    // device's reference count when successful.
    zx_device_t* dev = this->zxdev_;
    this->zxdev_ = nullptr;
    return device_remove_deprecated(dev);
  }

  // Schedules the removal of the device and its descendents.
  // Each device will evenutally have its unbind hook (if implemented) and release hook invoked.
  void DdkAsyncRemove() {
    ZX_ASSERT(this->zxdev_ != nullptr);

    zx_device_t* dev = this->zxdev_;
    device_async_remove(dev);
  }

  zx_status_t DdkGetMetadataSize(uint32_t type, size_t* out_size) {
    // Uses parent() instead of zxdev() as metadata is usually checked
    // before DdkAdd(). There are few use cases to actually call it on self.
    return device_get_metadata_size(parent(), type, out_size);
  }

  zx_status_t DdkGetMetadata(uint32_t type, void* buf, size_t buf_len, size_t* actual) {
    // Uses parent() instead of zxdev() as metadata is usually checked
    // before DdkAdd(). There are few use cases to actually call it on self.
    return device_get_metadata(parent(), type, buf, buf_len, actual);
  }

  zx_status_t DdkAddMetadata(uint32_t type, const void* data, size_t length) {
    return device_add_metadata(zxdev(), type, data, length);
  }

  zx_status_t DdkPublishMetadata(const char* path, uint32_t type, const void* data, size_t length) {
    return device_publish_metadata(zxdev(), path, type, data, length);
  }

  zx_status_t DdkScheduleWork(void (*callback)(void*), void* cookie) {
    return device_schedule_work(zxdev(), callback, cookie);
  }

  const char* name() const { return zxdev() ? device_get_name(zxdev()) : nullptr; }

  // The opaque pointer representing this device.
  zx_device_t* zxdev() const { return this->zxdev_; }
  // The opaque pointer representing the device's parent.
  zx_device_t* parent() const { return this->parent_; }

  void SetState(zx_signals_t stateflag) { device_state_set(this->zxdev_, stateflag); }

  void ClearState(zx_signals_t stateflag) { device_state_clr(this->zxdev_, stateflag); }

  void ClearAndSetState(zx_signals_t clearflag, zx_signals_t setflag) {
    device_state_clr_set(this->zxdev_, clearflag, setflag);
  }

 protected:
  Device(zx_device_t* parent) : internal::base_device<D, Mixins...>(parent) {
    internal::CheckMixins<Mixins<D>...>();
    internal::CheckReleasable<D>();
  }

 private:
  // Add the protocol id and ops if D inherits from a base_protocol implementation.
  template <typename T = D>
  void AddProtocol(
      device_add_args_t* args,
      typename std::enable_if<internal::is_base_protocol<T>::value, T>::type* dummy = 0) {
    auto dev = static_cast<D*>(this);
    ZX_ASSERT(dev->ddk_proto_id_ > 0);
    args->proto_id = dev->ddk_proto_id_;
    args->proto_ops = dev->ddk_proto_ops_;
  }

  // If D does not inherit from a base_protocol implementation, do nothing.
  template <typename T = D>
  void AddProtocol(
      device_add_args_t* args,
      typename std::enable_if<!internal::is_base_protocol<T>::value, T>::type* dummy = 0) {}
};

// Convenience type for implementations that would like to override all
// zx_protocol_device_t methods.
template <class D>
using FullDevice = Device<D, GetProtocolable, Initializable, Openable, Closable, UnbindableNew,
                          Readable, Writable, GetSizable, SuspendableNew, Resumable, Rxrpcable>;

}  // namespace ddk

#endif  // DDKTL_DEVICE_H_
