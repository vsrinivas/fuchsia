// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_CPP_FLOG_H_
#define APPS_MEDIA_CPP_FLOG_H_

namespace mojo {
namespace flog {

// NOTE: This is a temporary version of flog.h that turns flog off.

//
// FORMATTED LOGGING
//
// The Flog class and associated macros provide a means of logging 'formatted'
// log messages serialized by Mojo. Flog uses an instance of FlogLogger to
// log events to the FlogService. Messages pulled from the FlogService can be
// deserialized using Mojo on behalf of log visualization and analysis tools.
//
// Message logging is performed using a 'channel', which is bound to a Mojo
// proxy for a particular interface. Mojo interfaces used for this purpose must
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

#define FLOG_INITIALIZE(shell, label) ((void)0)
#define FLOG_DESTROY() ((void)0)
#define FLOG_CHANNEL_DECL(channel_type, channel_name)
#define FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, subject)
#define FLOG(channel_name, call) ((void)0)
#define FLOG_ID(channel_name) 0

// Same as FLOG_CHANNEL_WITH_SUBJECT but supplies the address of |this| as
// the subject address. This is the preferred form for declaring channels that
// are instance members.
#define FLOG_INSTANCE_CHANNEL(channel_type, channel_name) \
  FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, FLOG_ADDRESS(this))

// Same as FLOG_CHANNEL_WITH_SUBJECT but supplies a null subject address.
#define FLOG_CHANNEL(channel_type, channel_name) \
  FLOG_CHANNEL_WITH_SUBJECT(channel_type, channel_name, 0)

}  // namespace flog
}  // namespace mojo

#endif  // APPS_MEDIA_CPP_FLOG_H_
