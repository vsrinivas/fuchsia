// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <atomic>
#include <ostream>
#include <string>
#include <tuple>
#include <type_traits>

namespace overnet {

// Status codes, chosen to be compatible with gRPC & Abseil
enum class StatusCode : uint8_t {
  /// Not an error; returned on success.
  OK = 0,

  /// The operation was cancelled (typically by the caller).
  CANCELLED = 1,

  /// Unknown error. An example of where this error may be returned is if a
  /// Status value received from another address space belongs to an error-space
  /// that is not known in this address space. Also errors raised by APIs that
  /// do not return enough error information may be converted to this error.
  UNKNOWN = 2,

  /// Client specified an invalid argument. Note that this differs from
  /// FAILED_PRECONDITION. INVALID_ARGUMENT indicates arguments that are
  /// problematic regardless of the state of the system (e.g., a malformed file
  /// name).
  INVALID_ARGUMENT = 3,

  /// Deadline expired before operation could complete. For operations that
  /// change the state of the system, this error may be returned even if the
  /// operation has completed successfully. For example, a successful response
  /// from a server could have been delayed long enough for the deadline to
  /// expire.
  DEADLINE_EXCEEDED = 4,

  /// Some requested entity (e.g., file or directory) was not found.
  NOT_FOUND = 5,

  /// Some entity that we attempted to create (e.g., file or directory) already
  /// exists.
  ALREADY_EXISTS = 6,

  /// The caller does not have permission to execute the specified operation.
  /// PERMISSION_DENIED must not be used for rejections caused by exhausting
  /// some resource (use RESOURCE_EXHAUSTED instead for those errors).
  /// PERMISSION_DENIED must not be used if the caller can not be identified
  /// (use UNAUTHENTICATED instead for those errors).
  PERMISSION_DENIED = 7,

  /// The request does not have valid authentication credentials for the
  /// operation.
  UNAUTHENTICATED = 16,

  /// Some resource has been exhausted, perhaps a per-user quota, or perhaps the
  /// entire file system is out of space.
  RESOURCE_EXHAUSTED = 8,

  /// Operation was rejected because the system is not in a state required for
  /// the operation's execution. For example, directory to be deleted may be
  /// non-empty, an rmdir operation is applied to a non-directory, etc.
  ///
  /// A litmus test that may help a service implementor in deciding
  /// between FAILED_PRECONDITION, ABORTED, and UNAVAILABLE:
  ///  (a) Use UNAVAILABLE if the client can retry just the failing call.
  ///  (b) Use ABORTED if the client should retry at a higher-level
  ///      (e.g., restarting a read-modify-write sequence).
  ///  (c) Use FAILED_PRECONDITION if the client should not retry until
  ///      the system state has been explicitly fixed. E.g., if an "rmdir"
  ///      fails because the directory is non-empty, FAILED_PRECONDITION
  ///      should be returned since the client should not retry unless
  ///      they have first fixed up the directory by deleting files from it.
  ///  (d) Use FAILED_PRECONDITION if the client performs conditional
  ///      REST Get/Update/Delete on a resource and the resource on the
  ///      server does not match the condition. E.g., conflicting
  ///      read-modify-write on the same resource.
  FAILED_PRECONDITION = 9,

  /// The operation was aborted, typically due to a concurrency issue like
  /// sequencer check failures, transaction aborts, etc.
  ///
  /// See litmus test above for deciding between FAILED_PRECONDITION, ABORTED,
  /// and UNAVAILABLE.
  ABORTED = 10,

  /// Operation was attempted past the valid range. E.g., seeking or reading
  /// past end of file.
  ///
  /// Unlike INVALID_ARGUMENT, this error indicates a problem that may be fixed
  /// if the system state changes. For example, a 32-bit file system will
  /// generate INVALID_ARGUMENT if asked to read at an offset that is not in the
  /// range [0,2^32-1], but it will generate OUT_OF_RANGE if asked to read from
  /// an offset past the current file size.
  ///
  /// There is a fair bit of overlap between FAILED_PRECONDITION and
  /// OUT_OF_RANGE. We recommend using OUT_OF_RANGE (the more specific error)
  /// when it applies so that callers who are iterating through a space can
  /// easily look for an OUT_OF_RANGE error to detect when they are done.
  OUT_OF_RANGE = 11,

  /// Operation is not implemented or not supported/enabled in this service.
  UNIMPLEMENTED = 12,

  /// Internal errors. Means some invariants expected by underlying System has
  /// been broken. If you see one of these errors, Something is very broken.
  INTERNAL = 13,

  /// The service is currently unavailable. This is a most likely a transient
  /// condition and may be corrected by retrying with a backoff.
  ///
  /// \warning Although data MIGHT not have been transmitted when this
  /// status occurs, there is NOT A GUARANTEE that the server has not seen
  /// anything. So in general it is unsafe to retry on this status code
  /// if the call is non-idempotent.
  ///
  /// See litmus test above for deciding between FAILED_PRECONDITION, ABORTED,
  /// and UNAVAILABLE.
  UNAVAILABLE = 14,

  /// Unrecoverable data loss or corruption.
  DATA_LOSS = 15,
};

const char* StatusCodeString(StatusCode s);

namespace status_impl {

struct StatusPayload final {
  std::atomic<uint32_t> refs;
  std::string reason;
};

struct StatusRep final {
  std::atomic<uint32_t> refs;
  StatusCode code;
  std::string reason;
};

extern const std::string empty_string;

}  // namespace status_impl

template <class T>
class StatusOr;

// A result from some operation.
// Currently a status code and a string reason, although this may be extended in
// the future to also capture additional data in the case of errors.
// Contains an optimization whereby Status objects containing no reason string
// are carried without allocation.
class Status final {
 public:
  Status(StatusCode code) : code_(static_cast<uintptr_t>(code)) {}
  Status(StatusCode code, std::string reason)
      : rep_(new status_impl::StatusRep{{1}, code, std::move(reason)}) {}
  ~Status() {
    static_assert(sizeof(Status) == sizeof(void*),
                  "sizeof(Status) should be one pointer");
    if (is_code_only()) return;
    if (rep_->refs.fetch_add(-1, std::memory_order_acq_rel) == 1) delete rep_;
  }

  Status(Status&& other) : code_(other.code_) { other.code_ = 0; }
  Status(const Status& other) : code_(other.code_) {
    if (!is_code_only()) {
      rep_->refs.fetch_add(1, std::memory_order_relaxed);
    }
  }
  Status& operator=(const Status& other) {
    Status copy(other);
    Swap(&copy);
    return *this;
  }
  Status& operator=(Status&& other) {
    Swap(&other);
    return *this;
  }

  void Swap(Status* other) { std::swap(code_, other->code_); }

  static const Status& Ok() { return Static<>::Ok; }
  static const Status& Cancelled() { return Static<>::Cancelled; }

  const Status& OrCancelled() const {
    if (is_ok()) return Cancelled();
    return *this;
  }

  StatusCode code() const {
    return is_code_only() ? static_cast<StatusCode>(code_) : rep_->code;
  }
  bool is_ok() const { return code() == StatusCode::OK; }
  bool is_error() const { return !is_ok(); }
  const std::string& reason() const {
    return is_code_only() ? status_impl::empty_string : rep_->reason;
  }

  template <class T>
  StatusOr<typename std::remove_reference<T>::type> Or(T&& value) const;

  template <class F>
  auto Then(F fn) -> decltype(fn()) {
    if (is_error()) return *this;
    return fn();
  }

 private:
  bool is_code_only() const { return code_ < 256; }

  template <int I = 0>
  struct Static {
    static const Status Ok;
    static const Status Cancelled;
  };

  union {
    status_impl::StatusRep* rep_;
    uintptr_t code_;
  };
};

template <int I>
const Status Status::Static<I>::Ok{StatusCode::OK};
template <int I>
const Status Status::Static<I>::Cancelled{StatusCode::CANCELLED};

inline std::ostream& operator<<(std::ostream& out, const Status& status) {
  out << StatusCodeString(status.code());
  if (status.reason().length()) {
    out << "(" << status.reason() << ")";
  }
  return out;
}

// Either a status or a T - allows carrying a separate object in the case of
// success.
template <class T>
class StatusOr final {
 public:
  using element_type = T;

  template <typename U, typename = typename std::enable_if<
                            !std::is_convertible<U, Status>::value &&
                            !std::is_convertible<U, StatusOr>::value>::type>
  StatusOr(U&& value)
      : code_(StatusCode::OK), storage_(std::forward<U>(value)) {}
  StatusOr(StatusCode code, const std::string& description)
      : code_(code),
        storage_(new status_impl::StatusPayload{{1}, description}) {
    if (code_ == StatusCode::OK) abort();
  }
  // Raise an untyped (but non-ok) Status object to a StatusOr
  StatusOr(const Status& status) : StatusOr(status.code(), status.reason()) {}
  StatusOr(const StatusOr& other) : code_(other.code_) {
    if (is_ok()) {
      new (&storage_) Storage(*other.unwrap());
    } else {
      auto* p = other.unwrap_err();
      new (&storage_) Storage(p);
      p->refs.fetch_add(1, std::memory_order_relaxed);
    }
  }
  StatusOr(StatusOr&& other) : code_(other.code_) {
    if (is_ok()) {
      new (&storage_) Storage(std::move(*other.unwrap()));
    } else {
      // TODO(ctiller): consider a payload-less variant of StatusOr failures so
      // we can omit the fetch_add here
      auto* p = other.unwrap_err();
      new (&storage_) Storage(p);
      p->refs.fetch_add(1, std::memory_order_relaxed);
    }
  }
  StatusOr& operator=(const StatusOr& other) {
    if (&other == this) return *this;
    this->~StatusOr();
    new (this) StatusOr(other);
    return *this;
  }
  ~StatusOr() {
    if (is_ok()) {
      unwrap()->~T();
    } else {
      auto* p = unwrap_err();
      if (p->refs.fetch_add(-1, std::memory_order_acq_rel) == 1) delete p;
    }
  }

  StatusCode code() const { return code_; }
  bool is_ok() const { return code() == StatusCode::OK; }
  bool is_error() const { return !is_ok(); }
  const std::string& reason() const {
    return is_ok() ? status_impl::empty_string : unwrap_err()->reason;
  }

  // Return (a pointer-to) an object on successful completion, or nullptr on
  // failure
  const T* get() const {
    if (is_ok()) return unwrap();
    return nullptr;
  }

  T* get() {
    if (is_ok()) return unwrap();
    return nullptr;
  }

  const T& value() const { return *unwrap(); }

  const T* operator->() const { return unwrap(); }
  T* operator->() { return unwrap(); }
  const T& operator*() const { return *unwrap(); }
  T& operator*() { return *unwrap(); }

  // Lower to an untyped status object
  Status AsStatus() const {
    if (reason().empty()) return Status(code());
    return Status(code(), reason());
  }

  template <class F>
  auto Then(F fn) const -> decltype(fn(*get())) {
    if (is_error()) return AsStatus();
    return fn(*get());
  }

 private:
  const T* unwrap() const {
    assert(is_ok());
    return &storage_.ok;
  }

  T* unwrap() {
    assert(is_ok());
    return &storage_.ok;
  }

  status_impl::StatusPayload* unwrap_err() const {
    assert(is_error());
    return storage_.payload;
  }

  StatusCode code_;
  union Storage {
    Storage() {}
    ~Storage() {}
    template <class U>
    Storage(U&& v) : ok(std::forward<U>(v)) {}
    Storage(status_impl::StatusPayload* p) : payload(p) {}
    T ok;
    status_impl::StatusPayload* payload;
  };
  Storage storage_;
};

template <class T>
StatusOr<typename std::remove_reference<T>::type> Status::Or(T&& t) const {
  if (is_error()) {
    return *this;
  } else {
    return std::forward<T>(t);
  }
}

template <class T>
inline std::ostream& operator<<(std::ostream& out, const StatusOr<T>& status) {
  out << StatusCodeString(status.code());
  if (status.reason().length()) {
    out << "(" << status.reason() << ")";
  }
  if (status.is_ok()) {
    out << ":" << status.value();
  }
  return out;
}

}  // namespace overnet
