// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_VAAPI_TEST_VAAPI_STUBS_H_
#define SRC_MEDIA_CODEC_CODECS_VAAPI_TEST_VAAPI_STUBS_H_

#include <va/va.h>

// Use to set all the return values to their respective default
void vaDefaultStubSetReturn();

// Set the return value for the vaCreateConfig stub
void vaCreateConfigStubSetReturn(VAStatus status);

// Set the return value for the vaCreateContext stub
void vaCreateContextStubSetReturn(VAStatus status);

// Set the return value for the vaCreateSurfaces stub
void vaCreateSurfacesStubSetReturn(VAStatus status);

#endif  // SRC_MEDIA_CODEC_CODECS_VAAPI_TEST_VAAPI_STUBS_H_
