// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_DEVICE_H_
#define DDKTL_DEVICE_H_

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <type_traits>
#include <zircon/assert.h>

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
// +----------------------+----------------------------------------------------+
// | Mixin class          | Required function implementation                   |
// +----------------------+----------------------------------------------------+
// | ddk::GetProtocolable | zx_status_t DdkGetProtocol(uint32_t proto_id,      |
// |                      |                            void* out)              |
// |                      |                                                    |
// | ddk::Openable        | zx_status_t DdkOpen(zx_device_t** dev_out,         |
// |                      |                     uint32_t flags)                |
// |                      |                                                    |
// | ddk::OpenAtable      | zx_status_t DdkOpenAt(zx_device_t** dev_out,       |
// |                      |                       const char* path,            |
// |                      |                       uint32_t flags)              |
// |                      |                                                    |
// | ddk::Closable        | zx_status_t DdkClose(uint32_t flags)               |
// |                      |                                                    |
// | ddk::Unbindable      | void DdkUnbind()                                   |
// |                      |                                                    |
// | ddk::Readable        | zx_status_t DdkRead(void* buf, size_t count,       |
// |                      |                     zx_off_t off, size_t* actual)  |
// |                      |                                                    |
// | ddk::Writable        | zx_status_t DdkWrite(const void* buf,              |
// |                      |                      size_t count, zx_off_t off,   |
// |                      |                      size_t* actual)               |
// |                      |                                                    |
// | ddk::GetSizable      | zx_off_t DdkGetSize()                              |
// |                      |                                                    |
// | ddk::Ioctlable       | zx_status_t DdkIoctl(uint32_t op,                  |
// |                      |                      const void* in_buf,           |
// |                      |                      size_t in_len, void* out_buf, |
// |                      |                      size_t out_len,               |
// |                      |                      size_t* actual)               |
// |                      |                                                    |
// | ddk::Messageable     | zx_status_t DdkMessage(fidl_msg_t* msg,            |
// |                      |                        fidl_txn_t* txn)            |
// |                      |                                                    |
// | ddk::Suspendable     | zx_status_t DdkSuspend(uint32_t flags)             |
// |                      |                                                    |
// | ddk::Resumable       | zx_status_t DdkResume(uint32_t flags)              |
// |                      |                                                    |
// | ddk::Rxrpcable       | zx_status_t DdkRxrpc(zx_handle_t channel)          |
// +----------------------+----------------------------------------------------+
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
//                                          ddk::Readable, ddk::Unbindable>;
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
//     void DdkUnbind();
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
class OpenAtable : public base_mixin {
protected:
    static constexpr void InitOp(zx_protocol_device_t* proto) {
        internal::CheckOpenAtable<D>();
        proto->open_at = OpenAt;
    }

private:
    static zx_status_t OpenAt(void* ctx, zx_device_t** dev_out, const char* path,
                              uint32_t flags) {
        return static_cast<D*>(ctx)->DdkOpenAt(dev_out, path, flags);
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
        static_cast<D*>(ctx)->DdkUnbind();
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
    static zx_status_t Write(void* ctx, const void* buf, size_t count, zx_off_t off,
                             size_t* actual) {
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
    static zx_off_t GetSize(void* ctx) {
        return static_cast<D*>(ctx)->DdkGetSize();
    }
};

template <typename D>
class Ioctlable : public base_mixin {
protected:
    static constexpr void InitOp(zx_protocol_device_t* proto) {
        internal::CheckIoctlable<D>();
        proto->ioctl = Ioctl;
    }

private:
    static zx_status_t Ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
        return static_cast<D*>(ctx)->DdkIoctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    }
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
class Suspendable : public base_mixin {
protected:
    static constexpr void InitOp(zx_protocol_device_t* proto) {
        internal::CheckSuspendable<D>();
        proto->suspend = Suspend;
    }

private:
    static zx_status_t Suspend(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkSuspend(flags);
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
                       zx_handle_t client_remote = ZX_HANDLE_INVALID) {
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
        AddProtocol(&args);

        return device_add(this->parent_, &args, &this->zxdev_);
    }

    zx_status_t DdkAddComposite(const char* name, const zx_device_prop_t* props, size_t props_count,
                                const device_component_t* components, size_t components_count,
                                uint32_t coresident_device_index) {
        return device_add_composite(this->parent_, name, props, props_count, components,
                                    components_count, coresident_device_index);
    }

    void DdkMakeVisible() {
        device_make_visible(zxdev());
    }

    // Removes the device.
    // This method may have the side-effect of destroying this object if the
    // device's reference count drops to zero.
    zx_status_t DdkRemove() {
        if (this->zxdev_ == nullptr) {
            return ZX_ERR_BAD_STATE;
        }

        // The call to |device_remove| must be last since it decrements the
        // device's reference count when successful.
        zx_device_t* dev = this->zxdev_;
        this->zxdev_ = nullptr;
        return device_remove(dev);
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

    zx_status_t DdkPublishMetadata(const char* path, uint32_t type, const void* data,
                                   size_t length) {
        return device_publish_metadata(zxdev(), path, type, data, length);
    }

    const char* name() const { return zxdev() ? device_get_name(zxdev()) : nullptr; }

    // The opaque pointer representing this device.
    zx_device_t* zxdev() const { return this->zxdev_; }
    // The opaque pointer representing the device's parent.
    zx_device_t* parent() const { return this->parent_; }

    void SetState(zx_signals_t stateflag) {
        device_state_set(this->zxdev_, stateflag);
    }

    void ClearState(zx_signals_t stateflag) {
        device_state_clr(this->zxdev_, stateflag);
    }

    void ClearAndSetState(zx_signals_t clearflag, zx_signals_t setflag) {
        device_state_clr_set(this->zxdev_, clearflag, setflag);
    }

protected:
    Device(zx_device_t* parent)
        : internal::base_device<D, Mixins...>(parent) {
        internal::CheckMixins<Mixins<D>...>();
        internal::CheckReleasable<D>();
    }

private:
    // Add the protocol id and ops if D inherits from a base_protocol implementation.
    template <typename T = D>
    void AddProtocol(device_add_args_t* args,
                     typename std::enable_if<internal::is_base_protocol<T>::value, T>::type* dummy = 0) {
        auto dev = static_cast<D*>(this);
        ZX_ASSERT(dev->ddk_proto_id_ > 0);
        args->proto_id = dev->ddk_proto_id_;
        args->proto_ops = dev->ddk_proto_ops_;
    }

    // If D does not inherit from a base_protocol implementation, do nothing.
    template <typename T = D>
    void AddProtocol(device_add_args_t* args,
                     typename std::enable_if<!internal::is_base_protocol<T>::value, T>::type* dummy = 0) {}
};

// Convenience type for implementations that would like to override all
// zx_protocol_device_t methods.
template <class D>
using FullDevice = Device<D,
                          GetProtocolable,
                          Openable,
                          OpenAtable,
                          Closable,
                          Unbindable,
                          Readable,
                          Writable,
                          GetSizable,
                          Ioctlable,
                          Suspendable,
                          Resumable,
                          Rxrpcable>;

} // namespace ddk

#endif // DDKTL_DEVICE_H_
