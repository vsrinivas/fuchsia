// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_URL_URL_CONSTANTS_H_
#define LIB_URL_URL_CONSTANTS_H_

#include "lib/url/url_export.h"

namespace url {

URL_EXPORT extern const char kAboutBlankURL[];

URL_EXPORT extern const char kAboutScheme[];
URL_EXPORT extern const char kBlobScheme[];
URL_EXPORT extern const char kDataScheme[];
URL_EXPORT extern const char kFileScheme[];
URL_EXPORT extern const char kFtpScheme[];
URL_EXPORT extern const char kGopherScheme[];
URL_EXPORT extern const char kHttpScheme[];
URL_EXPORT extern const char kHttpsScheme[];
URL_EXPORT extern const char kJavaScriptScheme[];
URL_EXPORT extern const char kMailToScheme[];
URL_EXPORT extern const char kWsScheme[];
URL_EXPORT extern const char kWssScheme[];

// Used to separate a standard scheme and the hostname: "://".
URL_EXPORT extern const char kStandardSchemeSeparator[];

}  // namespace url

#endif  // LIB_URL_URL_CONSTANTS_H_
