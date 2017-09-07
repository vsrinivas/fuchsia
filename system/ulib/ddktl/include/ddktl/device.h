// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddktl/device-internal.h>
#include <magenta/assert.h>
#include <fbl/type_support.h>

// ddk::Device<D, ...>
//
// Notes:
//
// ddk::Device<D, ...> is a mixin class that simplifies writing DDK drivers in
// C++. The DDK's mx_device_t defines a set of function pointer callbacks that
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
// | ddk::GetProtocolable | mx_status_t DdkGetProtocol(uint32_t proto_id,      |
// |                      |                            void* out)              |
// |                      |                                                    |
// | ddk::Openable        | mx_status_t DdkOpen(mx_device_t** dev_out,         |
// |                      |                     uint32_t flags)                |
// |                      |                                                    |
// | ddk::OpenAtable      | mx_status_t DdkOpenAt(mx_device_t** dev_out,       |
// |                      |                       const char* path,            |
// |                      |                       uint32_t flags)              |
// |                      |                                                    |
// | ddk::Closable        | mx_status_t DdkClose(uint32_t flags)               |
// |                      |                                                    |
// | ddk::Unbindable      | void DdkUnbind()                                   |
// |                      |                                                    |
// | ddk::Readable        | mx_status_t DdkRead(void* buf, size_t count,       |
// |                      |                     mx_off_t off, size_t* actual)  |
// |                      |                                                    |
// | ddk::Writable        | mx_status_t DdkWrite(const void* buf,              |
// |                      |                      size_t count, mx_off_t off,   |
// |                      |                      size_t* actual)               |
// |                      |                                                    |
// | ddk::IotxnQueueable  | void DdkIotxnQueue(iotxn_t* txn)                   |
// |                      |                                                    |
// | ddk::GetSizable      | mx_off_t DdkGetSize()                              |
// |                      |                                                    |
// | ddk::Ioctlable       | mx_status_t DdkIoctl(uint32_t op,                  |
// |                      |                      const void* in_buf,           |
// |                      |                      size_t in_len, void* out_buf, |
// |                      |                      size_t out_len,               |
// |                      |                      size_t* actual)               |
// |                      |                                                    |
// | ddk::Suspendable     | mx_status_t DdkSuspend(uint32_t flags)             |
// |                      |                                                    |
// | ddk::Resumable       | mx_status_t DdkResume(uint32_t flags)              |
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
//     MyDevice(mx_device_t* parent)
//       : DeviceType(parent) {}
//
//     mx_status_t Bind() {
//         // Any other setup required by MyDevice. The device_add_args_t will be filled out by the
//         // base class.
//         return DdkAdd("my-device-name");
//     }
//
//     // Methods required by the ddk mixins
//     mx_status_t DdkOpen(mx_device_t** dev_out, uint32_t flags);
//     mx_status_t DdkClose(uint32_t flags);
//     mx_status_t DdkRead(void* buf, size_t count, mx_off_t off, size_t* actual);
//     void DdkUnbind();
//     void DdkRelease();
// };
//
// extern "C" mx_status_t my_bind(mx_device_t* device,
//                                void** cookie) {
//     auto dev = unique_ptr<MyDevice>(new MyDevice(device));
//     auto status = dev->Bind();
//     if (status == MX_OK) {
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
    explicit GetProtocolable(mx_protocol_device_t* proto) {
        internal::CheckGetProtocolable<D>();
        proto->get_protocol = GetProtocol;
    }

  private:
    static mx_status_t GetProtocol(void* ctx, uint32_t proto_id, void* out) {
        return static_cast<D*>(ctx)->DdkGetProtocol(proto_id, out);
    }
};

template <typename D>
class Openable : public internal::base_mixin {
  protected:
    explicit Openable(mx_protocol_device_t* proto) {
        internal::CheckOpenable<D>();
        proto->open = Open;
    }

  private:
    static mx_status_t Open(void* ctx, mx_device_t** dev_out, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkOpen(dev_out, flags);
    }
};

template <typename D>
class OpenAtable : public internal::base_mixin {
  protected:
    explicit OpenAtable(mx_protocol_device_t* proto) {
        internal::CheckOpenAtable<D>();
        proto->open_at = OpenAt;
    }

  private:
    static mx_status_t OpenAt(void* ctx, mx_device_t** dev_out, const char* path,
                              uint32_t flags) {
        return static_cast<D*>(ctx)->DdkOpenAt(dev_out, path, flags);
    }
};

template <typename D>
class Closable : public internal::base_mixin {
  protected:
    explicit Closable(mx_protocol_device_t* proto) {
        internal::CheckClosable<D>();
        proto->close = Close;
    }

  private:
    static mx_status_t Close(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkClose(flags);
    }
};

template <typename D>
class Unbindable : public internal::base_mixin {
  protected:
    explicit Unbindable(mx_protocol_device_t* proto) {
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
    explicit Readable(mx_protocol_device_t* proto) {
        internal::CheckReadable<D>();
        proto->read = Read;
    }

  private:
    static mx_status_t Read(void* ctx, void* buf, size_t count, mx_off_t off, size_t* actual) {
        return static_cast<D*>(ctx)->DdkRead(buf, count, off, actual);
    }
};

template <typename D>
class Writable : public internal::base_mixin {
  protected:
    explicit Writable(mx_protocol_device_t* proto) {
        internal::CheckWritable<D>();
        proto->write = Write;
    }

  private:
    static mx_status_t Write(void* ctx, const void* buf, size_t count, mx_off_t off,
                             size_t* actual) {
        return static_cast<D*>(ctx)->DdkWrite(buf, count, off, actual);
    }
};

template <typename D>
class IotxnQueueable : public internal::base_mixin {
  protected:
    explicit IotxnQueueable(mx_protocol_device_t* proto) {
        internal::CheckIotxnQueueable<D>();
        proto->iotxn_queue = IotxnQueue;
    }

  private:
    static void IotxnQueue(void* ctx, iotxn_t* txn) {
        static_cast<D*>(ctx)->DdkIotxnQueue(txn);
    }
};

template <typename D>
class GetSizable : public internal::base_mixin {
  protected:
    explicit GetSizable(mx_protocol_device_t* proto) {
        internal::CheckGetSizable<D>();
        proto->get_size = GetSize;
    }

  private:
    static mx_off_t GetSize(void* ctx) {
        return static_cast<D*>(ctx)->DdkGetSize();
    }
};

template <typename D>
class Ioctlable : public internal::base_mixin {
  protected:
    explicit Ioctlable(mx_protocol_device_t* proto) {
        internal::CheckIoctlable<D>();
        proto->ioctl = Ioctl;
    }

  private:
    static mx_status_t Ioctl(void* ctx, uint32_t op, const void* in_buf, size_t in_len,
                             void* out_buf, size_t out_len, size_t* out_actual) {
        return static_cast<D*>(ctx)->DdkIoctl(op, in_buf, in_len, out_buf, out_len, out_actual);
    }
};

template <typename D>
class Suspendable : public internal::base_mixin {
  protected:
    explicit Suspendable(mx_protocol_device_t* proto) {
        internal::CheckSuspendable<D>();
        proto->suspend = Suspend;
    }

  private:
    static mx_status_t Suspend(void* ctx, uint32_t flags) {
        return static_cast<D*>(ctx)->DdkSuspend(flags);
    }
};

template <typename D>
class Resumable : public internal::base_mixin {
  protected:
    explicit Resumable(mx_protocol_device_t* proto) {
        internal::CheckResumable<D>();
        proto->resume = Resume;
    }

  private:
    static mx_status_t Resume(void* ctx, uint32_t flags) {
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
    mx_status_t DdkAdd(const char* name) {
        if (mxdev_ != nullptr) {
            return MX_ERR_BAD_STATE;
        }

        device_add_args_t args = {};
        args.version = DEVICE_ADD_ARGS_VERSION;
        args.name = name;
        // Since we are stashing this as a D*, we can use ctx in all
        // the callback functions and cast it directly to a D*.
        args.ctx = static_cast<D*>(this);
        args.ops = &ddk_device_proto_;
        AddProtocol(&args);

        return device_add(parent_, &args, &mxdev_);
    }

    mx_status_t DdkRemove() {
        if (mxdev_ == nullptr) {
            return MX_ERR_BAD_STATE;
        }

        mx_status_t res = device_remove(mxdev_);
        mxdev_ = nullptr;
        return res;
    }

    const char* name() const { return mxdev() ? device_get_name(mxdev()) : nullptr; }

    // The opaque pointer representing this device.
    mx_device_t* mxdev() const { return mxdev_; }
    // The opaque pointer representing the device's parent.
    mx_device_t* parent() const { return parent_; }

    void SetState(mx_signals_t stateflag) {
        device_state_set(mxdev_, stateflag);
    }

    void ClearState(mx_signals_t stateflag) {
        device_state_clr(mxdev_, stateflag);
    }

    void ClearAndSetState(mx_signals_t clearflag, mx_signals_t setflag) {
        device_state_clr_set(mxdev_, clearflag, setflag);
    }

  protected:
    Device(mx_device_t* parent)
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
        MX_ASSERT(dev->ddk_proto_id_ > 0);
        args->proto_id = dev->ddk_proto_id_;
        args->proto_ops = dev->ddk_proto_ops_;
    }

    // If D does not inherit from a base_protocol implementation, do nothing.
    template <typename T = D>
    void AddProtocol(device_add_args_t* args,
                     typename fbl::enable_if<!is_protocol<T>::value, T>::type* dummy = 0) {}
};

// Convenience type for implementations that would like to override all
// mx_protocol_device_t methods.
template <class D>
using FullDevice = Device<D,
                          GetProtocolable,
                          Openable,
                          OpenAtable,
                          Closable,
                          Unbindable,
                          Readable,
                          Writable,
                          IotxnQueueable,
                          GetSizable,
                          Ioctlable,
                          Suspendable,
                          Resumable>;

}  // namespace ddk
