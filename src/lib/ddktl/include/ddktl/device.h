// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_H_

#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/fdf/cpp/channel.h>
#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/fidl/cpp/wire/message.h>
#include <lib/fidl/cpp/wire/traits.h>
#include <lib/fidl/cpp/wire/wire_messaging.h>
#include <lib/fidl_driver/cpp/transport.h>
#include <lib/stdcompat/span.h>
#include <lib/sys/component/cpp/service_client.h>
#include <lib/zx/channel.h>
#include <lib/zx/result.h>
#include <lib/zx/vmo.h>
#include <zircon/assert.h>

#include <deque>
#include <type_traits>

#include <ddktl/device-group.h>
#include <ddktl/device-internal.h>
#include <ddktl/fidl.h>
#include <ddktl/init-txn.h>
#include <ddktl/metadata.h>
#include <ddktl/resume-txn.h>
#include <ddktl/suspend-txn.h>
#include <ddktl/unbind-txn.h>

// ddk::Device<D, ...>
//
// Notes:
//
// ddk::Device<D, ...> is a mixin class that simplifies writing DDK drivers in
// C++. The DDK's zx_device_t defines a set of function pointer callbacks that
// can be implemented to define standard behavior (e.g., message),
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
// +----------------------------+----------------------------------------------------+
// | Mixin class                | Required function implementation                   |
// +----------------------------+----------------------------------------------------+
// | ddk::GetProtocolable       | zx_status_t DdkGetProtocol(uint32_t proto_id,      |
// |                            |                            void* out)              |
// |                            |                                                    |
// | ddk::Initializable         | void DdkInit(ddk::InitTxn txn)                     |
// |                            |                                                    |
// | ddk::Openable              | zx_status_t DdkOpen(zx_device_t** dev_out,         |
// |                            |                     uint32_t flags)                |
// |                            |                                                    |
// | ddk::Closable              | zx_status_t DdkClose(uint32_t flags)               |
// |                            |                                                    |
// | ddk::Unbindable            | void DdkUnbind(ddk::UnbindTxn txn)                 |
// |                            |                                                    |
// | ddk::PerformanceTunable    | zx_status_t DdkSetPerformanceState(                |
// |                            |                           uint32_t requested_state,|
// |                            |                           uint32_t* out_state)     |
// |                            |                                                    |
// | ddk::AutoSuspendable       | zx_status_t DdkConfigureAutoSuspend(bool enable,   |
// |                            |                      uint8_t requested_sleep_state)|
// |                            |                                                    |
// | ddk::Messageable<P>::Mixin | Methods defined by fidl::WireServer<P>             |
// |                            |                                                    |
// | ddk::Suspendable           | void DdkSuspend(ddk::SuspendTxn txn)               |
// |                            |                                                    |
// | ddk::Resumable             | zx_status_t DdkResume(uint8_t requested_state,     |
// |                            |                          uint8_t* out_state)       |
// |                            |                                                    |
// | ddk::Rxrpcable             | zx_status_t DdkRxrpc(zx_handle_t channel)          |
// +----------------------------+----------------------------------------------------+
//
//
// :: Example ::
//
// // Define our device type using a type alias.
// class MyDevice;
// using DeviceType = ddk::Device<MyDevice, ddk::Openable, ddk::Closable,
//                                          ddk::Unbindable, ddk::Suspendable>;
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
//     void DdkUnbind(ddk::UnbindTxn txn);
//     void DdkSuspend(ddk::SuspendTxn txn);
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
  const void* ops;
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
class Unbindable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckUnbindable<D>();
    proto->unbind = Unbind;
  }

 private:
  static void Unbind(void* ctx) {
    auto dev = static_cast<D*>(ctx);
    UnbindTxn txn(dev->zxdev());
    dev->DdkUnbind(std::move(txn));
  }
};

template <typename D>
class ServiceConnectable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckServiceConnectable<D>();
    proto->service_connect = ServiceConnect;
  }

 private:
  static zx_status_t ServiceConnect(void* ctx, const char* service_name, fdf_handle_t channel) {
    return static_cast<D*>(ctx)->DdkServiceConnect(service_name, fdf::Channel(channel));
  }
};

template <typename D, typename Protocol>
class MessageableInternal : public fidl::WireServer<Protocol>, public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) { proto->message = Message; }

 private:
  static zx_status_t Message(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    DdkTransaction transaction(txn);
    fidl::WireDispatch<Protocol>(static_cast<D*>(ctx),
                                 fidl::IncomingHeaderAndMessage::FromEncodedCMessage(msg),
                                 &transaction);
    return transaction.Status();
  }
};

template <typename Protocol>
struct Messageable {
  // This is necessary for currying as this mixin requires two type parameters, which are passed
  // at different times.

  template <typename D>
  using Mixin = MessageableInternal<D, Protocol>;
};

template <typename D>
class Suspendable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckSuspendable<D>();
    proto->suspend = Suspend_New;
  }

 private:
  static void Suspend_New(void* ctx, uint8_t requested_state, bool enable_wake,
                          uint8_t suspend_reason) {
    auto dev = static_cast<D*>(ctx);
    SuspendTxn txn(dev->zxdev(), requested_state, enable_wake, suspend_reason);
    static_cast<D*>(ctx)->DdkSuspend(std::move(txn));
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
class Resumable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckResumable<D>();
    proto->resume = Resume_New;
  }

 private:
  static void Resume_New(void* ctx, uint32_t requested_state) {
    auto dev = static_cast<D*>(ctx);
    ResumeTxn txn(dev->zxdev(), requested_state);
    static_cast<D*>(ctx)->DdkResume(std::move(txn));
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

template <typename D>
class Multibindable : public base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    internal::CheckMultibindable<D>();
    proto->open_protocol_session_multibindable = OpenProtocolSessionMultibindable;
    proto->close_protocol_session_multibindable = CloseProtocolSessionMultibindable;
  }

 private:
  static zx_status_t OpenProtocolSessionMultibindable(void* ctx, uint32_t proto_id, void* out) {
    return static_cast<D*>(ctx)->DdkOpenProtocolSessionMultibindable(proto_id, out);
  }
  static zx_status_t CloseProtocolSessionMultibindable(void* ctx, void* out) {
    return static_cast<D*>(ctx)->DdkCloseProtocolSessionMultibindable(out);
  }
};

class MetadataList {
 public:
  MetadataList() = default;
  MetadataList& operator=(const MetadataList& other) {
    data_list_.clear();
    metadata_list_.clear();
    ZX_ASSERT(other.metadata_list_.size() == other.data_list_.size());
    for (size_t i = 0; i < other.metadata_list_.size(); ++i) {
      data_list_.push_back(other.data_list_[i]);
      metadata_list_.push_back({other.metadata_list_[i].type, data_list_.back()->data(),
                                other.metadata_list_[i].length});
    }
    return *this;
  }
  MetadataList(const MetadataList& other) { *this = other; }
  zx_status_t AddMetadata(zx_device_t* dev, uint32_t type) {
    auto metadata_blob = GetMetadataBlob(dev, type);
    if (!metadata_blob.is_ok()) {
      return metadata_blob.error_value();
    }
    data_list_.emplace_back(
        std::make_shared<std::vector<uint8_t>>(metadata_blob->begin(), metadata_blob->end()));
    metadata_list_.push_back({type, data_list_.back()->data(), metadata_blob->size()});
    return ZX_OK;
  }

  device_metadata_t* data() { return metadata_list_.data(); }
  size_t count() { return metadata_list_.size(); }

 private:
  std::vector<std::shared_ptr<std::vector<uint8_t>>> data_list_;
  std::vector<device_metadata_t> metadata_list_;
};

class DeviceAddArgs {
 public:
  explicit DeviceAddArgs(const char* name) { args_.name = name; }
  DeviceAddArgs& operator=(const DeviceAddArgs& other) {
    metadata_list_ = std::move(other.metadata_list_);
    args_ = other.args_;
    args_.metadata_list = metadata_list_.data();
    args_.metadata_count = metadata_list_.count();
    return *this;
  }
  DeviceAddArgs(const DeviceAddArgs& other) { *this = other; }

  DeviceAddArgs& set_name(const char* name) {
    args_.name = name;
    return *this;
  }
  DeviceAddArgs& set_flags(uint32_t flags) {
    args_.flags = flags;
    return *this;
  }
  DeviceAddArgs& set_props(cpp20::span<const zx_device_prop_t> props) {
    args_.props = props.data();
    args_.prop_count = static_cast<uint32_t>(props.size());
    return *this;
  }
  DeviceAddArgs& set_str_props(cpp20::span<const zx_device_str_prop_t> props) {
    args_.str_props = props.data();
    args_.str_prop_count = static_cast<uint32_t>(props.size());
    return *this;
  }
  DeviceAddArgs& set_proto_id(uint32_t proto_id) {
    args_.proto_id = proto_id;
    return *this;
  }
  DeviceAddArgs& set_proxy_args(const char* proxy_args) {
    args_.proxy_args = proxy_args;
    return *this;
  }
  DeviceAddArgs& set_client_remote(zx::channel client_remote) {
    args_.client_remote = client_remote.release();
    return *this;
  }
  DeviceAddArgs& set_inspect_vmo(zx::vmo inspect_vmo) {
    args_.inspect_vmo = inspect_vmo.release();
    return *this;
  }
  DeviceAddArgs& forward_metadata(zx_device_t* dev, uint32_t type) {
    if (ZX_OK == metadata_list_.AddMetadata(dev, type)) {
      args_.metadata_list = metadata_list_.data();
      args_.metadata_count = metadata_list_.count();
    }
    return *this;
  }
  DeviceAddArgs& set_outgoing_dir(zx::channel outgoing_dir) {
    args_.outgoing_dir_channel = outgoing_dir.release();
    return *this;
  }
  DeviceAddArgs& set_fidl_protocol_offers(cpp20::span<const char*> fidl_protocol_offers) {
    args_.fidl_protocol_offers = fidl_protocol_offers.data();
    args_.fidl_protocol_offer_count = fidl_protocol_offers.size();
    return *this;
  }
  DeviceAddArgs& set_fidl_service_offers(cpp20::span<const char*> fidl_service_offers) {
    args_.fidl_service_offers = fidl_service_offers.data();
    args_.fidl_service_offer_count = fidl_service_offers.size();
    return *this;
  }
  DeviceAddArgs& set_runtime_service_offers(cpp20::span<const char*> runtime_service_offers) {
    args_.runtime_service_offers = runtime_service_offers.data();
    args_.runtime_service_offer_count = runtime_service_offers.size();
    return *this;
  }
  DeviceAddArgs& set_power_states(cpp20::span<const device_power_state_info_t> power_states) {
    args_.power_states = power_states.data();
    args_.power_state_count = static_cast<uint8_t>(power_states.size());
    return *this;
  }
  DeviceAddArgs& set_performance_states(
      cpp20::span<const device_performance_state_info_t> performance_states) {
    args_.performance_states = performance_states.data();
    args_.performance_state_count = static_cast<uint8_t>(performance_states.size());
    return *this;
  }

  const device_add_args_t& get() const { return args_; }

 private:
  MetadataList metadata_list_;
  device_add_args_t args_ = {};
};

// Device is templated on the list of mixins that define which DDK device
// methods are implemented. Note that internal::base_device *must* be the
// left-most base class in order to ensure that its constructor runs before the
// mixin constructors. This ensures that ddk_device_proto_ is zero-initialized
// before setting the fields in the mixins.
template <class D, template <typename> class... Mixins>
class Device : public ::ddk::internal::base_device<D, Mixins...> {
 public:
  zx_status_t DdkAdd(const char* name, device_add_args_t args) {
    if (this->zxdev_ != nullptr) {
      return ZX_ERR_BAD_STATE;
    }

    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = name;
    // Since we are stashing this as a D*, we can use ctx in all
    // the callback functions and cast it directly to a D*.
    args.ctx = static_cast<D*>(this);
    args.ops = &this->ddk_device_proto_;
    AddProtocol(&args);

    if (name) {
      this->name_ = name;
    }

    return device_add(this->parent_, &args, &this->zxdev_);
  }

  zx_status_t DdkAdd(DeviceAddArgs args) { return DdkAdd(args.get().name, args.get()); }

  zx_status_t DdkAdd(const char* name, uint32_t flags = 0) {
    return DdkAdd(ddk::DeviceAddArgs(name).set_flags(flags));
  }

  zx_status_t DdkAddComposite(const char* name, const composite_device_desc_t* comp_desc) {
    return device_add_composite(this->parent_, name, comp_desc);
  }

  zx_status_t DdkAddDeviceGroup(const char* name, DeviceGroupDesc group_desc) {
    return device_add_group(this->parent_, name, &group_desc.get());
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

  uint32_t DdkGetFragmentCount() { return device_get_fragment_count(parent()); }

  void DdkGetFragments(composite_device_fragment_t* comp_list, size_t comp_count,
                       size_t* comp_actual) {
    device_get_fragments(parent(), comp_list, comp_count, comp_actual);
  }

  zx_status_t DdkGetFragmentMetadata(const char* name, uint32_t type, void* buf, size_t buf_len,
                                     size_t* actual) {
    // Uses parent() instead of zxdev() as metadata is usually checked
    // before DdkAdd(). There are few use cases to actually call it on self.
    return device_get_fragment_metadata(parent(), name, type, buf, buf_len, actual);
  }

  zx_status_t DdkGetFragmentProtocol(const char* name, uint32_t proto_id, void* out) {
    return device_get_fragment_protocol(zxdev(), name, proto_id, out);
  }

  template <typename Protocol, typename = std::enable_if_t<fidl::IsProtocolV<Protocol>>>
  zx_status_t DdkConnectFidlProtocol(
      fidl::ServerEnd<Protocol> request,
      const char* path = fidl::DiscoverableProtocolName<Protocol>) const {
    static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::ChannelTransport>);

    return device_connect_fidl_protocol(parent(), path, request.TakeChannel().release());
  }

  template <typename Protocol, typename = std::enable_if_t<fidl::IsProtocolV<Protocol>>>
  zx_status_t DdkConnectFragmentFidlProtocol(
      const char* fragment_name, fidl::ServerEnd<Protocol> request,
      const char* path = fidl::DiscoverableProtocolName<Protocol>) const {
    static_assert(std::is_same_v<typename Protocol::Transport, fidl::internal::ChannelTransport>);

    return device_connect_fragment_fidl_protocol(parent(), fragment_name, path,
                                                 request.TakeChannel().release());
  }

  template <typename ServiceMember,
            typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
  zx::result<fidl::ClientEnd<typename ServiceMember::ProtocolType>> DdkConnectFidlProtocol() const {
    static_assert(std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::ChannelTransport>);

    return DdkConnectFidlProtocol<ServiceMember>(parent());
  }

  template <typename ServiceMember,
            typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
  static zx::result<fidl::ClientEnd<typename ServiceMember::ProtocolType>> DdkConnectFidlProtocol(
      zx_device_t* parent) {
    static_assert(std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::ChannelTransport>);

    auto endpoints = fidl::CreateEndpoints<typename ServiceMember::ProtocolType>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    auto status =
        device_connect_fidl_protocol2(parent, ServiceMember::ServiceName, ServiceMember::Name,
                                      endpoints->server.TakeChannel().release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(endpoints->client));
  }

  template <typename ServiceMember,
            typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
  zx::result<fidl::ClientEnd<typename ServiceMember::ProtocolType>> DdkConnectFragmentFidlProtocol(
      const char* fragment_name) const {
    static_assert(std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::ChannelTransport>);

    return DdkConnectFragmentFidlProtocol<ServiceMember>(parent(), fragment_name);
  }

  template <typename ServiceMember,
            typename = std::enable_if_t<fidl::IsServiceMemberV<ServiceMember>>>
  static zx::result<fidl::ClientEnd<typename ServiceMember::ProtocolType>>
  DdkConnectFragmentFidlProtocol(zx_device_t* parent, const char* fragment_name) {
    static_assert(std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::ChannelTransport>);

    auto endpoints = fidl::CreateEndpoints<typename ServiceMember::ProtocolType>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    auto status = device_connect_fragment_fidl_protocol2(
        parent, fragment_name, ServiceMember::ServiceName, ServiceMember::Name,
        endpoints->server.TakeChannel().release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(endpoints->client));
  }

  zx_status_t DdkServiceConnect(const char* service_name, fdf::Channel channel) {
    return device_service_connect(parent(), service_name, channel.release());
  }

  template <typename ServiceMember>
  zx::result<fdf::ClientEnd<typename ServiceMember::ProtocolType>> DdkConnectRuntimeProtocol()
      const {
    static_assert(fidl::IsServiceMemberV<ServiceMember>);
    static_assert(std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::DriverTransport>);

    return DdkConnectRuntimeProtocol<ServiceMember>(parent());
  }

  template <typename ServiceMember>
  static zx::result<fdf::ClientEnd<typename ServiceMember::ProtocolType>> DdkConnectRuntimeProtocol(
      zx_device_t* parent) {
    static_assert(fidl::IsServiceMemberV<ServiceMember>);
    static_assert(std::is_same_v<typename ServiceMember::ProtocolType::Transport,
                                 fidl::internal::DriverTransport>);

    auto endpoints = fdf::CreateEndpoints<typename ServiceMember::ProtocolType>();
    if (endpoints.is_error()) {
      return endpoints.take_error();
    }

    auto status =
        device_connect_runtime_protocol(parent, ServiceMember::ServiceName, ServiceMember::Name,
                                        endpoints->server.TakeChannel().release());
    if (status != ZX_OK) {
      return zx::error(status);
    }
    return zx::ok(std::move(endpoints->client));
  }

  const char* name() const { return this->name_.c_str(); }

  // The opaque pointer representing this device.
  zx_device_t* zxdev() const { return this->zxdev_; }
  // The opaque pointer representing the device's parent.
  zx_device_t* parent() const { return this->parent_; }

 protected:
  explicit Device(zx_device_t* parent) : internal::base_device<D, Mixins...>(parent) {
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

}  // namespace ddk

// Each of these Mixins are deprecated. Please do not add new usages, and please
// remove existing uses.
//
// Deprecated Mixins:
// +----------------------------+----------------------------------------------------+
// | Mixin class                | Required function implementation                   |
// +----------------------------+----------------------------------------------------+
// | ddk_deprecated::Readable   | zx_status_t DdkRead(void* buf, size_t count,       |
// |                            |                     zx_off_t off, size_t* actual)  |
// |                            |                                                    |
// | ddk_deprecated::Writable   | zx_status_t DdkWrite(const void* buf,              |
// |                            |                      size_t count, zx_off_t off,   |
// |                            |                      size_t* actual)               |
// |                            |                                                    |
// | ddk_deprecated::GetSizable | zx_off_t DdkGetSize()                              |
// |                            |                                                    |
// +----------------------------+----------------------------------------------------+
//
namespace ddk_deprecated {
#if defined(DDKTL_ALLOW_READ_WRITE)
template <typename D>
class Readable : public ddk::base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    ddk::internal::CheckReadable<D>();
    proto->read = Read;
  }

 private:
  static zx_status_t Read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
    return static_cast<D*>(ctx)->DdkRead(buf, count, off, actual);
  }
};

template <typename D>
class Writable : public ddk::base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    ddk::internal::CheckWritable<D>();
    proto->write = Write;
  }

 private:
  static zx_status_t Write(void* ctx, const void* buf, size_t count, zx_off_t off, size_t* actual) {
    return static_cast<D*>(ctx)->DdkWrite(buf, count, off, actual);
  }
};

#endif

#if defined(DDKTL_ALLOW_GETSIZABLE)
template <typename D>
class GetSizable : public ddk::base_mixin {
 protected:
  static constexpr void InitOp(zx_protocol_device_t* proto) {
    ddk::internal::CheckGetSizable<D>();
    proto->get_size = GetSize;
  }

 private:
  static zx_off_t GetSize(void* ctx) { return static_cast<D*>(ctx)->DdkGetSize(); }
};
#endif

}  // namespace ddk_deprecated

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_H_
