// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "register_io.h"

RegisterIo::RegisterIo(std::unique_ptr<magma::PlatformMmio> mmio) : mmio_(std::move(mmio)) {}
