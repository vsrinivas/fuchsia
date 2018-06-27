// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/testing/test_credentials_provider.h"

#include <lib/fit/function.h>

namespace cloud_provider_firestore {

TestCredentialsProvider::TestCredentialsProvider(async_t* async)
    : task_runner_(async) {}

TestCredentialsProvider::~TestCredentialsProvider() {}

void TestCredentialsProvider::GetCredentials(
    fit::function<void(std::shared_ptr<grpc::CallCredentials>)> callback) {
  task_runner_.PostTask(
      [callback = std::move(callback)] { callback(nullptr); });
}

}  // namespace cloud_provider_firestore
