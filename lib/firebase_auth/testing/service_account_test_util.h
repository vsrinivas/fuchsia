// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/network_wrapper/fake_network_wrapper.h>

namespace service_account {

namespace http = ::fuchsia::net::oldhttp;

std::string GetSuccessResponseBodyForTest(std::string token, size_t expiration);

http::URLResponse GetResponseForTest(http::HttpErrorPtr error, uint32_t status,
                                     std::string body);

}  // namespace service_account
