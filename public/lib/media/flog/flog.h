// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "lib/app/cpp/application_context.h"
#include "lib/media/fidl/flog/flog.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/message.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/handles/object_info.h"

namespace flog {

//
// FORMATTED LOGGING
//
// The Flog class and associated macros provide a means of logging 'formatted'
// log messages serialized by fidl. Flog uses an instance of FlogLogger to
// log events to the FlogService. Messages pulled from the FlogService can be
// deserialized using fidl on behalf of log visualization and analysis tools.
//
// Message logging is performed using a 'channel', which is bound to a fidl
// proxy for a particular interface. Fidl interfaces used for this purpose must
// be request-only, meaning the constituent methods must not have responses.
//
// Assume that we've defined the following interface:
//
//     [ServiceName="my_namespace::MyFlogChannelInterface"]
//     interface MyFlogChannelInterface {
//       Thing1(int64 a, int32 b);
//       Thing2(string c);
//     };
//
// Note that the ServiceName annotation is required.
//
// A channel instance may be defined as a member of a class as follows:
//
//     FLOG_INSTANCE_CHANNEL(MyFlogChannelInterface, my_flog_channel_);
//
// For cases in which the channel isn't a class instance member, the
// FLOG_CHANNEL macro is provided:
//
//     FLOG_CHANNEL(MyFlogChannelInterface, g_my_flog_channel);
//
// If NDEBUG is defined, these compile to nothing. Otherwise, they declare and
// initialize my_flog_channel_instance (or g_my_flog_channel), which can be used
// via the FLOG macro:
//
//     FLOG(my_flog_channel_, Thing1(1234, 5678));
//     FLOG(my_flog_channel_, Thing2("To the lifeboats!"));
//
// These invocations compile to nothing if NDEBUG is defined. Otherwise, they
// log messages to the channel represented by my_flog_channel_instance.
//
// FLOG_CHANNEL_DECL produces only a declaration for cases in which a channel
// must be declared but not defined (e.g. as a static class member).
//
// Logging to a channel does nothing unless the Flog class has been initialized
// with a call to Flog::Initialize (via the FLOG_INITIALIZE macro).
// Flog::Initialize provides a FlogLogger implementation to be used for logging.
// Typically, this implementation would be acquired from the FlogService using
// CreateLogger.
//

// Converts a pointer to a uint64_t for channel messages that have address
// parameters. Addresses can't be accessed by log consumers, but they can be
// used for identification.
#define FLOG_ADDRESS(p) reinterpret_cast<uintptr_t>(p)

#ifdef FLOG_ENABLED

// Initializes flog, connecting to the service and creating a new log. |shell|
// is the application's shell (for connecting to the service), and |label| is
// the log label, usually the name of the service or application. Should be
// called once on startup, usually in the OnInitialize override of the
// ApplicationImplBase subclass.
#define FLOG_INITIALIZE(application_context, label) \
  flog::Flog::Initialize(application_context, label)

// Destroys the resources created by FLOG_INITIALIZE. Should be called once on
// shutdown, usually in the destructor of the ApplicationImplBase subclass.
#define FLOG_DESTROY() flog::Flog::Destroy()

// Declares a flog channel but does not initialize it. This is useful when the
// declaration and definition must be separate.
#define FLOG_CHANNEL_DECL(channel_type, channel_name) \
  std::unique_ptr<flog::FlogProxy<channel_type>> channel_name

// Defines a variable with the indicated name (|channel_name|) and the indicated
// type (|channel_type|, which must be a fidl interface type). |subject_address|
// is provided to associate an address with the channel. Use FLOG_CHANNEL or
// FLOG_INSTANCE_CHANNEL instead of this macro unless there is a need to be
// specific about the subject. A |subject_address| value of 0 indicates there
// is no subject address for the channel.
#define FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, subject_address) \
  FLOG_CHANNEL_DECL(channel_type, channel_name) =                              \
      flog::FlogProxy<channel_type>::Create(subject_address)

// Logs a channel message on the specified channel (a name previously declared
// using FLOG_CHANNEL, FLOG_INSTANCE_CHANNEL, FLOG_CHANNEL_WITH_SUBJECT or
// FLOG_CHANNEL_DECL). |call| is a valid method call for the channel type. See
// the example above.
#define FLOG(channel_name, call) channel_name->call

// Gets the numeric channel id from a channel.
#define FLOG_ID(channel_name) channel_name->flog_channel()->id()

// The following four macros are used to obtain koids that can be used to
// unify both ends of a channel used in a fidl connection. FLOG_REQUEST_KOID
// and FLOG_BINDING_KOID are used at the service end of the connection.
// FLOG_PTR_KOID and FLOG_HANDLE_KOID are used at the client end of the
// connection. In all cases, the koid returned identifies the server end of
// the connection. On the client end, the service-end koid is obtained by
// getting the 'related' koid for the local channel.

// Returns the koid of the local channel associated with a pending
// InterfaceRequest. This value is identical to the value returned by
// FLOG_PTR_ID(pointer) where the request was created from pointer.
#define FLOG_REQUEST_KOID(request) flog::GetInterfaceRequestKoid(&request)

// Returns the koid of the local channel associated with a binding. This value
// is identical to the value returned by FLOG_PTR_ID(pointer) where pointer is
// the client end of the binding.
#define FLOG_BINDING_KOID(binding) mtl::GetKoid(binding.handle())

// Returns the koid of the remote channel associated with a bound InterfacePtr.
// This value is identical to the value return by FLOG_REQUEST_ID(request)
// where request was created from the pointer.
#define FLOG_PTR_KOID(ptr) flog::GetInterfacePtrRelatedKoid(&ptr)

// Returns the koid of the remote channel associated with a bound
// InterfaceHandle. This value is identical to the value return by
// FLOG_REQUEST_ID(request) where request was created from the handle.
#define FLOG_HANDLE_KOID(h) mtl::GetRelatedKoid(h.handle().get())

// Same as FLOG_CHANNEL_WITH_SUBJECT but supplies the address of |this| as
// the subject address. This is the preferred form for declaring channels that
// are instance members.
#define FLOG_INSTANCE_CHANNEL(channel_type, channel_name) \
  FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, FLOG_ADDRESS(this))

// Same as FLOG_CHANNEL_WITH_SUBJECT but supplies a null subject address.
#define FLOG_CHANNEL(channel_type, channel_name) \
  FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, 0)

// Thread-safe logger for all channels in a given process.
class Flog {
 public:
  static void Initialize(app::ApplicationContext* application_context,
                         const std::string& label);

  // Deletes the flog logger singleton.
  static void Destroy() {
    FTL_DCHECK(logger_);
    logger_.reset();
  }

  // Allocates a unique id for a new channel. Never returns 0.
  static uint32_t AllocateChannelId() { return ++last_allocated_channel_id_; }

  // Logs the creation of a channel.
  static void LogChannelCreation(uint32_t channel_id,
                                 const char* channel_type_name,
                                 uint64_t subject_address);

  // Logs a channel message.
  static void LogChannelMessage(uint32_t channel_id, fidl::Message* message);

  // Logs the deletion of a channel.
  static void LogChannelDeletion(uint32_t channel_id);

 private:
  // Gets the current time in nanoseconds since epoch.
  static int64_t GetTime();

  static std::atomic_ulong last_allocated_channel_id_;
  static FlogLoggerPtr logger_;
  static uint64_t next_entry_index_;
};

// Channel backing a FlogProxy.
class FlogChannel : public fidl::MessageReceiverWithResponder {
 public:
  FlogChannel(const char* channel_type_name, uint64_t subject_address);

  ~FlogChannel() override;

  // Returns the channel id.
  uint32_t id() const { return id_; }

  // MessageReceiverWithResponder implementation.
  bool Accept(fidl::Message* message) override;

  bool AcceptWithResponder(fidl::Message* message,
                           MessageReceiver* responder) override;

 private:
  uint32_t id_ = 0;
};

template <typename T>
class FlogProxy : public T::Proxy_ {
 public:
  static std::unique_ptr<FlogProxy<T>> Create(uint64_t subject_address) {
    return std::unique_ptr<FlogProxy<T>>(new FlogProxy<T>(subject_address));
  }

  FlogChannel* flog_channel() { return channel_.get(); }

 private:
  explicit FlogProxy(std::unique_ptr<FlogChannel> channel)
      : T::Proxy_(channel.get()), channel_(std::move(channel)) {}

  explicit FlogProxy(uint64_t subject_address)
      : FlogProxy(std::make_unique<FlogChannel>(T::Name_, subject_address)) {}

  std::unique_ptr<FlogChannel> channel_;
};

template <typename T>
mx_koid_t GetInterfaceRequestKoid(fidl::InterfaceRequest<T>* request) {
  FTL_DCHECK(request != nullptr);
  FTL_DCHECK(*request);
  mx::channel channel = request->PassChannel();
  mx_koid_t result = mtl::GetKoid(channel.get());
  request->Bind(std::move(channel));
  return result;
}

template <typename T>
mx_koid_t GetInterfacePtrRelatedKoid(fidl::InterfacePtr<T>* ptr) {
  FTL_DCHECK(ptr != nullptr);
  FTL_DCHECK(*ptr);
  fidl::InterfaceHandle<T> handle = ptr->PassInterfaceHandle();
  mx_koid_t result = mtl::GetRelatedKoid(handle.handle().get());
  ptr->Bind(std::move(handle));
  return result;
}

#else  // FLOG_ENABLED

#define FLOG_INITIALIZE(shell, label) ((void)0)
#define FLOG_DESTROY() ((void)0)
#define FLOG_CHANNEL_DECL(channel_type, channel_name)
#define FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, subject)
#define FLOG(channel_name, call) ((void)0)
#define FLOG_ID(channel_name) 0
#define FLOG_REQUEST_KOID(request) ((void)request, MX_KOID_INVALID)
#define FLOG_BINDING_KOID(binding) ((void)binding, MX_KOID_INVALID)
#define FLOG_PTR_KOID(ptr) ((void)ptr, MX_KOID_INVALID)
#define FLOG_HANDLE_KOID(h) ((void)h, MX_KOID_INVALID)
#define FLOG_INSTANCE_CHANNEL(channel_type, channel_name)
#define FLOG_CHANNEL(channel_type, channel_name)

#endif  // FLOG_ENABLED

}  // namespace flog
