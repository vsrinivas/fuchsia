// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_ASTRING_H_
#define GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_ASTRING_H_

#include <string>

// Some android source relies on this being pulled in:
#include <utils/Errors.h>

namespace android {

// For now this uses inheritance, but it should be possible to switch to
// delegation should there be any good reason to do so.
struct AString : public std::string {
 public:
  AString();
  AString(const char* string);  // implicit single-arg constructor intentional
  AString(const char* string, size_t size);
  AString(
      const AString& copy_from);  // implicit single-arg constructor intentional
  void append(int number_to_append);
  void append(const char* string_to_append);
  void append(const char* string_to_append, size_t size);
  void append(const AString& string_to_append);
  AString& operator=(const AString& assign_from);

 private:
};

AString AStringPrintf(const char* format, ...);

}  // namespace android

#endif  // GARNET_BIN_MEDIA_CODECS_SW_OMX_COMMON_OMX_ANDROID_PAL_INCLUDE_MEDIA_STAGEFRIGHT_FOUNDATION_ASTRING_H_
