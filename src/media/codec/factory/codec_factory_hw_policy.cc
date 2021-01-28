// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_factory_hw_policy.h"

CodecFactoryHwPolicy::CodecFactoryHwPolicy(Owner* owner) : owner_(owner) {}

CodecFactoryHwPolicy::~CodecFactoryHwPolicy() {}

async_dispatcher_t* CodecFactoryHwPolicy::dispatcher() { return owner_->dispatcher(); }
