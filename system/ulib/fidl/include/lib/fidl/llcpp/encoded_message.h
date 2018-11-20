// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_
#define LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fit/traits.h>
#include <type_traits>
#include <utility>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

namespace fidl {

// Holds an encoded FIDL message, that is, a byte array plus a handle table.
//
// The bytes part points to an external caller-managed buffer, while the handles part
// is owned by this class. Any handles will be closed upon destruction.
// This class is aware of the upper bound on the number of handles
// in a message, such that its size can be adjusted to fit the demands
// of a specific FIDL type.
//
// Because this class does not own the underlying message buffer, the caller
// must make sure the lifetime of this class does not extend over that of the buffer.
template <typename FidlType>
class EncodedMessage final {
    static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
    static_assert(FidlType::MaxSize > 0, "Positive message size");

public:
    // Instantiates an empty buffer with no bytes or handles.
    EncodedMessage() = default;

    EncodedMessage(EncodedMessage&& other) noexcept {
        if (this != &other) {
            MoveImpl(std::move(other));
        }
    }

    EncodedMessage& operator=(EncodedMessage&& other) noexcept {
        if (this != &other) {
            MoveImpl(std::move(other));
        }
        return *this;
    }

    EncodedMessage(const EncodedMessage& other) = delete;

    EncodedMessage& operator=(const EncodedMessage& other) = delete;

    // Instantiates an EncodedMessage which points to a buffer region with caller-managed memory.
    // It does not take ownership of that buffer region.
    // Also initializes an empty handles part.
    EncodedMessage(BytePart bytes) :
        bytes_(std::move(bytes)) { }

    ~EncodedMessage() {
        CloseHandles();
    }

    // Takes ownership of the contents of the message.
    // The bytes and handle parts will become empty, while the existing bytes part is returned.
    // The caller is responsible for having transferred the handles elsewhere
    // before calling this method.
    BytePart ReleaseBytesAndHandles() {
        handles_.set_actual(0);
        BytePart return_bytes = std::move(bytes_);
        return return_bytes;
    }

    const BytePart& bytes() const { return bytes_; }

    const HandlePart& handles() const { return handles_; }

    // Clears the contents of the EncodedMessage then invokes Callback
    // to initialize the EncodedMessage in-place then returns the callback's
    // result.
    //
    // |callback| is a callable object whose arguments are (BytePart&, HandlePart&).
    template <typename Callback>
    decltype(auto) Initialize(Callback callback) {
        static_assert(
            fit::callable_traits<Callback>::args::size == 2 &&
            std::is_same<
                BytePart&,
                typename fit::template callable_traits<Callback>::args::template at<0>>::value &&
            std::is_same<
                HandlePart&,
                typename fit::template callable_traits<Callback>::args::template at<1>>::value,
            "Callback signature must be: T(BytePart&, HandlePart&).");
        bytes_ = BytePart();
        CloseHandles();
        return callback(bytes_, handles_);
    }

private:
    void CloseHandles() {
        if (handles_.actual() > 0) {
#ifdef __Fuchsia__
            zx_handle_close_many(handles_.data(), handles_.actual());
#else
            // How did we have handles if not on Fuchsia? Something bad happened...
            assert(false);
#endif
            handles_.set_actual(0);
        }
    }

    void MoveImpl(EncodedMessage&& other) noexcept {
        CloseHandles();
        bytes_ = std::move(other.bytes_);
        // copy handles from |other|
        memcpy(handle_storage_, other.handle_storage_,
            other.handles_.actual() * sizeof(zx_handle_t));
        // release handles in |other|
        handles_.set_actual(other.handles().actual());
        other.handles_.set_actual(0);
    }

    BytePart bytes_;
    zx_handle_t handle_storage_[FidlType::MaxNumHandles];
    HandlePart handles_{handle_storage_, FidlType::MaxNumHandles};
};

}

#endif  // LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_
