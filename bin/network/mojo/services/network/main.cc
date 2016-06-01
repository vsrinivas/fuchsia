// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/c/system/main.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/application/run_application.h"

#include "network_service_delegate.h"

MojoResult MojoMain(MojoHandle shell_handle) {
  NetworkServiceDelegate network_service_delegate;
  return mojo::RunApplication(shell_handle, &network_service_delegate);
}
