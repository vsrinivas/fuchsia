// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TEST_CONSTANTS_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TEST_CONSTANTS_H_

#include "src/lib/fxl/strings/string_view.h"

// This file contains constant strings used to test code that needs a
// Credentials.
namespace service_account {

// Expected value for the Credentials members.
constexpr fxl::StringView kTestServiceAccountProjectId = "fake_project_id";
constexpr fxl::StringView kTestServiceAccountClientEmail =
    "fake_email@example.com";
constexpr fxl::StringView kTestServiceAccountClientId = "fake_client_id";

// Correct test json configuration file for Credentials.
constexpr fxl::StringView kTestServiceAccountConfig =
    "{"
    "\"project_id\": \"fake_project_id\","
    "\"private_key\": \""
    "-----BEGIN RSA PRIVATE KEY-----\\n"
    "MIIBOQIBAAJBALTlyNACX5j/oFWdgy/KvAZL9qj+eNuhXGBSvQM9noaPKqVhOXFH\\n"
    "hycW+TBzlHzj4Ga5uGtVJNzZaxdpfbqxV1cCAwEAAQJAZDJShESMRuZwHHveSf51\\n"
    "Hte8i+ZHcv9xdzjc0Iq037pGGmHh/TiNNZPtqgVbxQuGGdGQqJ54DMpz3Ja2ck1V\\n"
    "wQIhAOMyXwq0Se8+hCXFFFIo6QSVpDn5ZnXTyz+GBdiwkVXZAiEAy9TIRCCUd9j+\\n"
    "cy77lTCx6k6Pw5lY1LM5jTUR7dAD6K8CIBie1snUK8bvYWauartUj5vdk4Rs0Huo\\n"
    "Tfg+T9fhmn5RAiB5nfEL7SCIzbksgqjroE1Xjx5qR5Hf/zvki/ixmz7p0wIgdxLS\\n"
    "T/hN67jcu9a+/2geGTnk1ku2nhVlxS7UPCTq0os=\\n"
    "-----END RSA PRIVATE KEY-----"
    "\","
    "\"client_email\": \"fake_email@example.com\","
    "\"client_id\": \"fake_client_id\""
    "}";

// Incorrect test json configuration file.
constexpr fxl::StringView kWrongKeyTestServiceAccountConfig =
    "{"
    "\"project_id\": \"fake_project_id\","
    "\"private_key\": \""
    "-----BEGIN DSA PRIVATE KEY-----\\n"
    "MIH4AgEAAkEAteW2IBzioOu0aNGrQFv5RZ6VxS8NAyuNwvOrmjq8pxJSzTyrwD52\\n"
    "9XJmNVVXv/UWKvyPtr0rzrsJVpSzCEwaewIVAJT9/8i3lQrQEeACuO9bwzaG28Lh\\n"
    "AkAnvmU9Ogz6eTof5V58Lv1f8uKF6ZujgVb+Wc1gudx8wKIexKUBhE7rsnJUfLYw\\n"
    "HMXC8xZ5XJTEYog2U0vLKke7AkEApEq8XBO8qwEzP3VicpC/Huxa/zNZ2lveNgWm\\n"
    "tr089fvp3PSf4DwKTOKGZyg9NYsOSCfaCSvkWMeFCW4Y7XTpTAIUV9YTY3SlInIv\\n"
    "Ho2twE3HuzNZpLQ=\\n"
    "-----END DSA PRIVATE KEY-----\\n"
    "\","
    "\"client_email\": \"fake_email@example.com\","
    "\"client_id\": \"fake_id\""
    "}";

}  // namespace service_account

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TEST_CONSTANTS_H_
