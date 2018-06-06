// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_SO_ENTRY_POINT_SO_ENTRY_POINT_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_SO_ENTRY_POINT_SO_ENTRY_POINT_H_

#include <OMX_Component.h>

// For __EXPORT
#include <zircon/compiler.h>

__EXPORT
extern "C" void entrypoint_createSoftOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks, OMX_PTR appData,
    OMX_COMPONENTTYPE **component);

typedef void (*createSoftOMXComponent_fn)(const char *name,
                                          const OMX_CALLBACKTYPE *callbacks,
                                          OMX_PTR appData,
                                          OMX_COMPONENTTYPE **component);

// This is only available if the .so's code is linked as a static lib instead.
// This is not the normal way, but can be a faster debug cycle than using the
// .so. If linking as an .so, this symbol won't be available, so trying to call
// it will just fail to link.
extern "C" void direct_createSoftOMXComponent(const char *name,
                                              const OMX_CALLBACKTYPE *callbacks,
                                              OMX_PTR appData,
                                              OMX_COMPONENTTYPE **component);

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_SO_ENTRY_POINT_SO_ENTRY_POINT_H_
