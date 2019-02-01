// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_CUTILS_PROPERTIES_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_CUTILS_PROPERTIES_H_

#define PROPERTY_VALUE_MAX 92

extern "C" {

// This stub never finds anything.
int property_get(const char* key, char* value, const char* default_value);
}

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_CUTILS_PROPERTIES_H_
