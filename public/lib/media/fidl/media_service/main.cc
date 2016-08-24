// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/media_service_impl.h"
#include "mojo/public/c/include/mojo/system/main.h"
#include "mojo/public/cpp/application/run_application.h"

MojoResult MojoMain(MojoHandle application_request) {
  mojo::ScopedChromiumInit init;
  mojo::media::MediaFactoryService media_factory_service;
  return mojo::RunApplication(application_request, &media_factory_service);
}
