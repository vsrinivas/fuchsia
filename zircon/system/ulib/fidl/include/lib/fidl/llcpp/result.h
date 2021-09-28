// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_RESULT_H_
#define LIB_FIDL_LLCPP_RESULT_H_

#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <iosfwd>

#ifdef __Fuchsia__
#include <zircon/status.h>
#endif  // __Fuchsia__

namespace fidl {

// Reason for a failed operation, or how the endpoint was unbound from the
// client/server message dispatcher.
//
// |Reason| is always carried inside a |fidl::Result| or |fidl::UnbindInfo|. As
// such, it is always accompanied with a |status| value. The documentation below
// describes precise semantics of the |status| under different reasons.
//
// TODO(fxbug.dev/75702): Consider encapsulating this enum behind more
// ergonomic APIs that better matches the use cases around errors.
enum class Reason {
  // The value zero is reserved as a sentinel value that indicates an
  // uninitialized reason; it will never be returned to user code. So we don't
  // expose it here.

  // The user invoked `Unbind()`.
  //
  // If this reason is observed when making a call or sending an event or reply,
  // it indicates that the client/server endpoint has already been unbound, and
  // |status| will be |ZX_ERR_CANCELED|.
  //
  // If this reason is observed in an on-unbound handler in |fidl::UnbindInfo|,
  // |status| will be |ZX_OK|, since it indicates part of normal operation.
  kUnbind = 1,

  // The user invoked `Close(epitaph)` on a |fidl::ServerBindingRef| or
  // Completer and the epitaph was sent.
  //
  // This reason is only observable when part from a |fidl::UnbindInfo|.
  //
  // |status| is the result of sending the epitaph.
  kClose,

  // The endpoint peer was closed. For a server, |status| is ZX_ERR_PEER_CLOSED.
  // For a client, it is the epitaph. If no epitaph was sent, the behavior is
  // equivalent to having received a ZX_ERR_PEER_CLOSED epitaph.
  kPeerClosed,

  // An error associated with the dispatcher, or with waiting on the transport.
  //
  // |status| contains the associated error code.
  // For a server, the user is still responsible for sending an epitaph,
  // if they desire.
  kDispatcherError,

  // An error associated with reading to/writing from the transport e.g.
  // channel, that is not of type "peer closed".
  //
  // |status| contains the associated error code.
  // For a server, the user is still responsible for sending an epitaph,
  // if they desire.
  kTransportError,

  // Failure to encode an outgoing message, or converting an encoded message
  // to its incoming format (tests or in-process use cases).
  //
  // |status| contains the associated error code.
  // For a server, the user is still responsible for sending an epitaph,
  // if they desire.
  kEncodeError,

  // Failure to decode an incoming message.
  //
  // |status| contains the associated error code.
  // For a server, the user is still responsible for sending an epitaph,
  // if they desire.
  kDecodeError,

  // A malformed message, message with unknown ordinal, unexpected reply, or an
  // unsupported event was received.
  //
  // |status| contains the associated error code.
  // For a server, the user is still responsible for sending an epitaph,
  // if they desire.
  kUnexpectedMessage,
};

// |ErrorOrigin| indicates in which part of request/response processing did a
// particular error occur.
enum class ErrorOrigin {
  // Reading from the transport, decoding, running business logic, etc.
  kReceive,

  // Writing to the transport, encoding, etc.
  kSend,
};

namespace internal {

// A sentinel value that indicates an uninitialized reason. It should never be
// exposed to the user.
constexpr inline static ::fidl::Reason kUninitializedReason = static_cast<::fidl::Reason>(0);
static_assert(static_cast<int>(::fidl::Reason{}) == static_cast<int>(kUninitializedReason));

//
// Predefined error messages.
//

extern const char* const kErrorInvalidHeader;
extern const char* const kErrorUnknownTxId;
extern const char* const kErrorUnknownOrdinal;
extern const char* const kErrorTransport;
extern const char* const kErrorChannelUnbound;
extern const char* const kErrorWaitOneFailed;

}  // namespace internal

// |Result| represents the result of an operation.
//
// If the operation was successful:
// - `ok()` returns true.
// - `status()` returns ZX_OK.
// - `reason()` should not be used.
//
// If the operation failed:
// - `ok()` returns false.
// - `status()` contains a non-OK status code specific to the failed operation.
// - `reason()` describes the operation which failed.
//
// |Result| may be piped to an output stream (`std::cerr`, `FX_LOGS`, ...) to
// print a human-readable description for debugging purposes.
class Result {
 public:
  constexpr Result() = default;
  ~Result() = default;

  // Constructs a result representing a success.
  constexpr static Result Ok() { return Result(ZX_OK, internal::kUninitializedReason, nullptr); }

  // Constructs a result indicating that the operation cannot proceed
  // because the corresponding endpoint has been unbound from the dispatcher
  // (applies to both client and server).
  constexpr static Result Unbound() {
    return Result(ZX_ERR_CANCELED, ::fidl::Reason::kUnbind, ::fidl::internal::kErrorChannelUnbound);
  }

  // Constructs a result indicating that the operation cannot proceed
  // because a unknown message was received. Specifcally, the method or event
  // ordinal is not recognized by the binding.
  constexpr static Result UnknownOrdinal() {
    return Result(ZX_ERR_NOT_SUPPORTED, ::fidl::Reason::kUnexpectedMessage,
                  ::fidl::internal::kErrorUnknownOrdinal);
  }

  // Constructs a transport error with |status| and optional |error_message|.
  // |status| must not be |ZX_OK|.
  constexpr static Result TransportError(zx_status_t status, const char* error_message = nullptr) {
    ZX_DEBUG_ASSERT(status != ZX_OK);
    // Depending on the order of operations during a remote endpoint closure, we
    // may either observe a |kTransportError| from writing to a channel or a
    // peer closed notification from the dispatcher loop, which is less than
    // ideal and racy behavior. To squash this race, if a transport failed with
    // the |ZX_ERR_PEER_CLOSED| error code, we always consider the reason to be
    // |kPeerClosed|.
    ::fidl::Reason reason = Reason::kTransportError;
    if (status == ZX_ERR_PEER_CLOSED) {
      reason = Reason::kPeerClosed;
    }
    return Result(status, reason, error_message);
  }

  constexpr static Result EncodeError(zx_status_t status, const char* error_message = nullptr) {
    return Result(status, ::fidl::Reason::kEncodeError, error_message);
  }

  constexpr static Result DecodeError(zx_status_t status, const char* error_message = nullptr) {
    return Result(status, ::fidl::Reason::kDecodeError, error_message);
  }

  constexpr static Result UnexpectedMessage(zx_status_t status,
                                            const char* error_message = nullptr) {
    return Result(status, ::fidl::Reason::kUnexpectedMessage, error_message);
  }

  constexpr Result(const Result& result) = default;
  constexpr Result& operator=(const Result& result) = default;

  // Status associated with the reason. See documentation on |fidl::Reason|
  // for how to interpret the status.
  //
  // Generally, logging this status alone wouldn't be very useful, since its
  // interpretation is dependent on the reason.
  // Prefer logging |error| or via |FormatDescription|.
  [[nodiscard]] zx_status_t status() const { return status_; }

#ifdef __Fuchsia__
  // Returns the string representation of the status value.
  [[nodiscard]] const char* status_string() const { return zx_status_get_string(status_); }
#endif  // __Fuchsia__

  // A high-level reason for the failure.
  //
  // Generally, logging this value alone wouldn't be the most convenient for
  // debugging, since it requires developers to check back to the enum.
  // Prefer logging |error| or via |FormatDescription|.
  [[nodiscard]] ::fidl::Reason reason() const {
    ZX_ASSERT(reason_ != internal::kUninitializedReason);
    return reason_;
  }

  // Renders a full description of the success or error.
  //
  // It is more specific than |reason| alone e.g. if an encoding error was
  // encountered, it would contain a string description of the specific encoding
  // problem.
  //
  // If a logging API supports output streams (`<<` operators), piping the
  // |Result| to the log via `<<` is more efficient than calling this function.
  [[nodiscard]] std::string FormatDescription() const;

  // Returns a lossy description of the error. The returned |const char*| has
  // static lifetime, hence may be retained or passed around. If the result is a
  // success, returns |nullptr|.
  //
  // Because of this constraint, the bindings will attempt to pick a static
  // string that best represents the error, sometimes losing information. As
  // such, this method should only be used when interfacing with C APIs that are
  // unable to take a string or output stream.
  [[nodiscard]] const char* lossy_description() const;

  // If the operation was successful.
  [[nodiscard]] bool ok() const { return status_ == ZX_OK; }

  // If the operation failed, returns information about the error.
  //
  // This is meant be used by subclasses to accommodate a usage style that is
  // similar to |fitx::result| types:
  //
  //   fidl::WireResult bar = fidl::WireCall(foo_client_end).GetBar();
  //   if (!bar.ok()) {
  //     FX_LOGS(ERROR) << "GetBar failed: " << bar.error();
  //   }
  //
  const Result& error() const {
    ZX_ASSERT(status_ != ZX_OK);
    return *this;
  }

 protected:
  void SetResult(const Result& other) { operator=(other); }

  // Returns a pointer to populate additional error message.
  [[nodiscard]] const char** error_address() { return &error_; }

  // A human readable description of |reason|.
  [[nodiscard]] const char* reason_description() const;

  // Renders the description into a buffer |destination| that is of size
  // |length|. The description will cut off at `length - 1`.
  //
  // |from_unbind_info| should be true iff this is invoked by |UnbindInfo|.
  //
  // Returns how many bytes were written.
  size_t FormatImpl(char* destination, size_t length, bool from_unbind_info) const;

 private:
  friend class UnbindInfo;
  friend std::ostream& operator<<(std::ostream& ostream, const Result& result);

  __ALWAYS_INLINE
  constexpr Result(zx_status_t status, ::fidl::Reason reason, const char* error)
      : status_(status), reason_(reason), error_(error) {}

  zx_status_t status_ = ZX_ERR_INTERNAL;
  ::fidl::Reason reason_ = internal::kUninitializedReason;
  const char* error_ = nullptr;
};

// Logs a full description of the result to an output stream.
std::ostream& operator<<(std::ostream& ostream, const Result& result);

// |UnbindInfo| describes how the channel was unbound from a server or client.
//
// The reason is always initialized when part of a |fidl::UnbindInfo|.
//
// |UnbindInfo| is passed to |OnUnboundFn| and |AsyncEventHandler::Unbound| if
// provided by the user.
class UnbindInfo : private Result {
 public:
  // Creates an invalid |UnbindInfo|.
  constexpr UnbindInfo() = default;
  ~UnbindInfo() = default;

  constexpr explicit UnbindInfo(const Result& result) : Result(result) {
    ZX_DEBUG_ASSERT(reason() != internal::kUninitializedReason);
  }

  constexpr static UnbindInfo UnknownOrdinal() { return UnbindInfo{Result::UnknownOrdinal()}; }

  // Constructs an |UnbindInfo| indicating that the user explicitly requested
  // unbinding the server endpoint from the dispatcher.
  //
  // **Note that this is not the same as |Result::Unbound|**:
  // |Result::Unbound| means an operation failed because the required endpoint
  // has been unbound, and is an error. |UnbindInfo::Unbind| on the other hand
  // is an expected result from user initiation.
  constexpr static UnbindInfo Unbind() {
    return UnbindInfo{Result(ZX_OK, ::fidl::Reason::kUnbind, nullptr)};
  }

  // Constructs an |UnbindInfo| indicating that the server connection is
  // closed explicitly by the user. |status| is the status of writing
  // the epitaph to the channel. This is specific to the server bindings.
  //
  // Internally in the bindings runtine, |status| is also used to indicate
  // which epitaph value should be sent. But this is not re-exposed to the user
  // since the user provided the epitaph in the first place.
  constexpr static UnbindInfo Close(zx_status_t status) {
    return UnbindInfo{Result(status, ::fidl::Reason::kClose, nullptr)};
  }

  // Constructs an |UnbindInfo| indicating that the endpoint peer has
  // closed.
  constexpr static UnbindInfo PeerClosed(zx_status_t status) {
    return UnbindInfo{Result(status, ::fidl::Reason::kPeerClosed, nullptr)};
  }

  // Constructs an |UnbindInfo| indicating the async dispatcher returned
  // an error |status|.
  constexpr static UnbindInfo DispatcherError(zx_status_t status) {
    return UnbindInfo{Result(status, ::fidl::Reason::kDispatcherError, nullptr)};
  }

  UnbindInfo(const UnbindInfo&) = default;
  UnbindInfo& operator=(const UnbindInfo&) = default;

  // Reason for unbinding the channel.
  //
  // Generally, logging this value alone wouldn't be the most convenient for
  // debugging, since it requires developers to check back to the enum.
  // Prefer logging the |UnbindInfo| itself or via |FormatDescription|.
  using Result::reason;

  // Status associated with the reason. See documentation on |fidl::Reason|
  // for how to interpret the status.
  //
  // Generally, logging this status alone wouldn't be very useful, since its
  // interpretation is dependent on the reason.
  // Prefer logging the |UnbindInfo| itself or via |FormatDescription|.
  using Result::status;

#ifdef __Fuchsia__
  // Returns the string representation of the status value.
  using Result::status_string;
#endif  // __Fuchsia__

  // Renders a full description of the cause of the unbinding.
  //
  // It is more specific than |reason| alone e.g. if an encoding error was
  // encountered, it would contain a string description of the specific encoding
  // problem.
  //
  // If a logging API supports output streams (`<<` operators), piping the
  // |UnbindInfo| to the log via `<<` is more efficient than calling this
  // function.
  [[nodiscard]] std::string FormatDescription() const;

  // Returns a lossy description of the unbind cause. The returned |const char*| has
  // static lifetime, hence may be retained or passed around. If the result is a
  // success, returns |nullptr|.
  //
  // Because of this constraint, the bindings will attempt to pick a static
  // string that best represents the cause, sometimes losing information. As
  // such, this method should only be used when interfacing with C APIs that are
  // unable to take a string or output stream.
  using Result::lossy_description;

  // Returns true iff the unbinding was part of normal operation
  // (i.e. unbinding/closing that is explicitly initiated by the user),
  // as opposed to in response of an error/peer closed.
  [[nodiscard]] bool ok() const {
    switch (reason()) {
      case internal::kUninitializedReason:
        return false;
      case Reason::kUnbind:
      case Reason::kClose:
        return true;
      case Reason::kPeerClosed:
        // |ZX_OK| epitaph is considered an expectecd protocol termination.
        return status() == ZX_OK;
      default:
        return false;
    }
  }

  // Reinterprets the |UnbindInfo| as the cause of an operation failure.
  fidl::Result ToError() const { return Result(*this); }

 private:
  friend std::ostream& operator<<(std::ostream& ostream, const UnbindInfo& info);
};

// Logs a full description of the cause of unbinding to an output stream.
std::ostream& operator<<(std::ostream& ostream, const UnbindInfo& info);

static_assert(sizeof(UnbindInfo) == sizeof(uintptr_t) * 2, "UnbindInfo should be reasonably small");

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_RESULT_H_
