// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TEST_UTIL_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TEST_UTIL_H_

#include <lib/network_wrapper/fake_network_wrapper.h>

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

std::string GetSuccessResponseBodyForTest(std::string token, size_t expiration);

http::URLResponse GetResponseForTest(http::HttpErrorPtr error, uint32_t status, std::string body);

}  // namespace service_account

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TEST_UTIL_H_
