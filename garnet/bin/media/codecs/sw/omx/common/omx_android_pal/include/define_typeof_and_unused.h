// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_DEFINE_TYPEOF_AND_UNUSED_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_DEFINE_TYPEOF_AND_UNUSED_H_

// On android this is a GCC intrinsic, but on Fuchsia we use clang.
#ifndef typeof
#define typeof __typeof__
#endif

// On android this would come from bionic/libc/include/sys/cdefs.h, but on
// Fuchsia we don't want to bring in Android's libc.
#define __unused

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_DEFINE_TYPEOF_AND_UNUSED_H_
