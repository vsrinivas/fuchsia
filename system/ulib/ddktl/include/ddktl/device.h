// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <fbl/type_support.h>

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
// | ddk::Suspendable     | zx_status_t DdkSuspend(uint32_t flags)             |
// |                      |                                                    |
// | ddk::Resumable       | zx_status_t DdkResume(uint32_t flags)              |
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
//     auto dev = unique_ptr<MyDevice>(new MyDevice(device));
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

// DDK Device mixins

template <typename D>
class GetProtocolable : public internal::base_mixin {
  protected:
    explicit GetProtocolable(zx_protocol_device_t* proto) {
        internal::CheckGetProtocolable<D>();
        proto->get_protocol = GetProtocol;
    }

  private:
    static zx_status_t GetProtocol(void* ctx, uint32_t proto_id, void* out) {
        return static_cast<D*>(ctx)->DdkGetProtocol(proto_id, out);
    }
};

template <typename D>
class Openable : public internal::base_mixin {
  protected:
    explicit Openable(zx_protocol_device_t* proto) {
        internal::CheckOpenable<D>();
        proto->open = Open;
    }

  private:
    static zx_status_t Open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkOpen(dev_out, flags);
    }
};

template <typename D>
class OpenAtable : public internal::base_mixin {
  protected:
    explicit OpenAtable(zx_protocol_device_t* proto) {
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
class Closable : public internal::base_mixin {
  protected:
    explicit Closable(zx_protocol_device_t* proto) {
        internal::CheckClosable<D>();
        proto->close = Close;
    }

  private:
    static zx_status_t Close(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkClose(flags);
    }
};

template <typename D>
class Unbindable : public internal::base_mixin {
  protected:
    explicit Unbindable(zx_protocol_device_t* proto) {
        internal::CheckUnbindable<D>();
        proto->unbind = Unbind;
    }

  private:
    static void Unbind(void* ctx) {
        static_cast<D*>(ctx)->DdkUnbind();
    }
};

template <typename D>
class Readable : public internal::base_mixin {
  protected:
    explicit Readable(zx_protocol_device_t* proto) {
        internal::CheckReadable<D>();
        proto->read = Read;
    }

  private:
    static zx_status_t Read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
        return static_cast<D*>(ctx)->DdkRead(buf, count, off, actual);
    }
};

template <typename D>
class Writable : public internal::base_mixin {
  protected:
    explicit Writable(zx_protocol_device_t* proto) {
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
class GetSizable : public internal::base_mixin {
  protected:
    explicit GetSizable(zx_protocol_device_t* proto) {
        internal::CheckGetSizable<D>();
        proto->get_size = GetSize;
    }

  private:
    static zx_off_t GetSize(void* ctx) {
        return static_cast<D*>(ctx)->DdkGetSize();
    }
};

template <typename D>
class Ioctlable : public internal::base_mixin {
  protected:
    explicit Ioctlable(zx_protocol_device_t* proto) {
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
class Suspendable : public internal::base_mixin {
  protected:
    explicit Suspendable(zx_protocol_device_t* proto) {
        internal::CheckSuspendable<D>();
        proto->suspend = Suspend;
    }

  private:
    static zx_status_t Suspend(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkSuspend(flags);
    }
};

template <typename D>
class Resumable : public internal::base_mixin {
  protected:
    explicit Resumable(zx_protocol_device_t* proto) {
        internal::CheckResumable<D>();
        proto->resume = Resume;
    }

  private:
    static zx_status_t Resume(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkResume(flags);
    }
};

// Device is templated on the list of mixins that define which DDK device
// methods are implemented. Note that internal::base_device *must* be the
// left-most base class in order to ensure that its constructor runs before the
// mixin constructors. This ensures that ddk_device_proto_ is zero-initialized
// before setting the fields in the mixins.
template <class D, template <typename> class... Mixins>
class Device : public ::ddk::internal::base_device, public Mixins<D>... {
  public:
    zx_status_t DdkAdd(const char* name, uint32_t flags = 0, zx_device_prop_t* props = nullptr,
                       uint32_t prop_count = 0) {
        if (zxdev_ != nullptr) {
            return ZX_ERR_BAD_STATE;
        }

        device_add_args_t args = {};
        args.version = DEVICE_ADD_ARGS_VERSION;
        args.name = name;
        // Since we are stashing this as a D*, we can use ctx in all
        // the callback functions and cast it directly to a D*.
        args.ctx = static_cast<D*>(this);
        args.ops = &ddk_device_proto_;
        args.flags = flags;
        args.props = props;
        args.prop_count = prop_count;
        AddProtocol(&args);

        return device_add(parent_, &args, &zxdev_);
    }

    void DdkMakeVisible() {
        device_make_visible(zxdev());
    }

    // Removes the device.
    // This method may have the side-effect of destroying this object if the
    // device's reference count drops to zero.
    zx_status_t DdkRemove() {
        if (zxdev_ == nullptr) {
            return ZX_ERR_BAD_STATE;
        }

        // The call to |device_remove| must be last since it decrements the
        // device's reference count when successful.
        zx_device_t* dev = zxdev_;
        zxdev_ = nullptr;
        return device_remove(dev);
    }

    zx_status_t DdkGetMetadata(uint32_t type, void* buf, size_t buf_len, size_t* actual) {
        return device_get_metadata(zxdev(), type, buf, buf_len, actual);
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
    zx_device_t* zxdev() const { return zxdev_; }
    // The opaque pointer representing the device's parent.
    zx_device_t* parent() const { return parent_; }

    void SetState(zx_signals_t stateflag) {
        device_state_set(zxdev_, stateflag);
    }

    void ClearState(zx_signals_t stateflag) {
        device_state_clr(zxdev_, stateflag);
    }

    void ClearAndSetState(zx_signals_t clearflag, zx_signals_t setflag) {
        device_state_clr_set(zxdev_, clearflag, setflag);
    }

  protected:
    Device(zx_device_t* parent)
      : internal::base_device(parent),
        Mixins<D>(&ddk_device_proto_)... {
        internal::CheckMixins<Mixins<D>...>();
        internal::CheckReleasable<D>();

        ddk_device_proto_.release = DdkReleaseThunk;
    }

  private:
    static void DdkReleaseThunk(void* ctx) {
        static_cast<D*>(ctx)->DdkRelease();
    }

    template <typename T>
    using is_protocol = fbl::is_base_of<internal::base_protocol, T>;

    // Add the protocol id and ops if D inherits from a base_protocol implementation.
    template <typename T = D>
    void AddProtocol(device_add_args_t* args,
                     typename fbl::enable_if<is_protocol<T>::value, T>::type* dummy = 0) {
        auto dev = static_cast<D*>(this);
        ZX_ASSERT(dev->ddk_proto_id_ > 0);
        args->proto_id = dev->ddk_proto_id_;
        args->proto_ops = dev->ddk_proto_ops_;
    }

    // If D does not inherit from a base_protocol implementation, do nothing.
    template <typename T = D>
    void AddProtocol(device_add_args_t* args,
                     typename fbl::enable_if<!is_protocol<T>::value, T>::type* dummy = 0) {}
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
                          Resumable>;

}  // namespace ddk
