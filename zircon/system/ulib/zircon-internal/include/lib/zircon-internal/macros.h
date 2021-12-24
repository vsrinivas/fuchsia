// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZIRCON_INTERNAL_MACROS_H_
#define LIB_ZIRCON_INTERNAL_MACROS_H_

#define KB (1024UL)
#define MB (1024UL * KB)
#define GB (1024UL * MB)

#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)

// Expands to a string literial containing the filename and line number of the
// point at which it is evaluated.  E.g. "/somedir/somefile.cc:123".
//
// TODO(maniscalco): Consider stripping the path off the filename component
// (think basename).
#define SOURCE_TAG __FILE__ ":" STRINGIFY(__LINE__)

#endif  // LIB_ZIRCON_INTERNAL_MACROS_H_
