// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_EXPORT_H_
#define ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_EXPORT_H_

// Preprocessor symbol to control what symbols we export in a .so:
// EXPORT is for symbols exported by the shared library.
// A second variant is supported which is for the non-shared case.
// A preprocessor symbol is provided by the build system to select which
// variant we are building: SHARED_LIBRARY, which if defined means we are
// building a shared library. Really, news at 11.
#if defined(SHARED_LIBRARY)
#define EXPORT __EXPORT
#else
#define EXPORT
#endif

#endif  // ZIRCON_SYSTEM_ULIB_TRACE_PROVIDER_EXPORT_H_
