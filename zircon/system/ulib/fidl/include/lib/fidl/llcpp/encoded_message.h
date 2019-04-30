// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_
#define LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <lib/fidl/cpp/message_part.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/internal_callable_traits.h>
#include <lib/fidl/llcpp/traits.h>
#include <type_traits>
#include <utility>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>
#endif

namespace fidl {

namespace internal {

// When |MaxNumHandles| is zero, |handle_storage| is always NULL.
// This way we avoid declaring a C array with zero number of elements.
template <uint32_t MaxNumHandles, typename Enabled = void>
class EncodedMessageHandleHolder;

template <uint32_t MaxNumHandles>
class EncodedMessageHandleHolder<MaxNumHandles, std::enable_if_t<(MaxNumHandles > 0)>> {
protected:
    constexpr static uint32_t kResolvedMaxHandles =
        MaxNumHandles > ZX_CHANNEL_MAX_MSG_HANDLES
            ? ZX_CHANNEL_MAX_MSG_HANDLES
            : MaxNumHandles;

    zx_handle_t* handle_storage() { return &handle_storage_[0]; }

private:
    zx_handle_t handle_storage_[kResolvedMaxHandles];
};

template <uint32_t MaxNumHandles>
class EncodedMessageHandleHolder<MaxNumHandles, std::enable_if_t<(MaxNumHandles == 0)>> {
protected:
    constexpr static uint32_t kResolvedMaxHandles = 0;

    zx_handle_t* handle_storage() { return nullptr; }
};

} // namespace internal

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
class EncodedMessage final : public internal::EncodedMessageHandleHolder<FidlType::MaxNumHandles> {
    static_assert(IsFidlType<FidlType>::value, "Only FIDL types allowed here");
    static_assert(FidlType::PrimarySize > 0, "Positive message size");

    using Super = internal::EncodedMessageHandleHolder<FidlType::MaxNumHandles>;

public:
    // The maximum number of handles allowed in a message of this type, given the constraints
    // of a zircon channel packet.
    constexpr static uint32_t kResolvedMaxHandles = Super::kResolvedMaxHandles;

    // Instantiates an empty buffer with no bytes or handles.
    EncodedMessage() = default;

    // Construct an |EncodedMessage| borrowing the bytes and taking ownership of handles in |msg|.
    // The number of handles in |msg| must not exceed |kResolvedMaxHandles|.
    explicit EncodedMessage(fidl_msg_t* msg) {
        bytes_ = fidl::BytePart(static_cast<uint8_t*>(msg->bytes), msg->num_bytes, msg->num_bytes);
        ZX_ASSERT(msg->num_handles <= kResolvedMaxHandles);
        if (kResolvedMaxHandles > 0) {
            memcpy(handles_.data(), msg->handles, sizeof(zx_handle_t) * msg->num_handles);
            for (uint32_t i = 0; i < msg->num_handles; i++) {
                msg->handles[i] = ZX_HANDLE_INVALID;
            }
            handles_.set_actual(msg->num_handles);
        } else {
            handles_.set_actual(0);
        }
    }

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
    explicit EncodedMessage(BytePart bytes) :
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

    // Take ownership of bytes and handles and assemble into a |fidl::Message|.
    Message ToAnyMessage() {
        return Message(std::move(bytes_), std::move(handles_));
    }

    // Clears the contents of the EncodedMessage then invokes Callback
    // to initialize the EncodedMessage in-place then returns the callback's result.
    //
    // |callback| is a callable object whose arguments are (BytePart*, HandlePart*).
    template <typename Callback>
    decltype(auto) Initialize(Callback callback) {
        struct GoldenCallback { void operator()(BytePart*, HandlePart*) {} };
        static_assert(
            internal::SameArguments<Callback, GoldenCallback>,
            "Callback signature must be: T(BytePart*, HandlePart*).");
        bytes_ = BytePart();
        CloseHandles();
        return callback(&bytes_, &handles_);
    }

private:
    void CloseHandles() {
        if (kResolvedMaxHandles == 0) {
            return;
        }
        if (handles_.actual() > 0) {
#ifdef __Fuchsia__
            ZX_DEBUG_ASSERT(handles_.actual() <= kResolvedMaxHandles);
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
#ifdef __Fuchsia__
        ZX_DEBUG_ASSERT(other.handles_.actual() <= kResolvedMaxHandles);
#endif
        if (kResolvedMaxHandles > 0) {
            // copy handles from |other|
            memcpy(Super::handle_storage(), other.Super::handle_storage(),
                   other.handles_.actual() * sizeof(zx_handle_t));
        }
        // release handles in |other|
        handles_.set_actual(other.handles().actual());
        other.handles_.set_actual(0);
    }

    BytePart bytes_;
    HandlePart handles_{Super::handle_storage(), kResolvedMaxHandles};
};

} // namespace fidl

#endif  // LIB_FIDL_LLCPP_ENCODED_MESSAGE_H_
