// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "ref_call_counter.h"

RefCallCounter::RefCallCounter() : add_ref_calls_(0u), release_calls_(0u) {}

void RefCallCounter::AddRef() { add_ref_calls_++; }
bool RefCallCounter::Release()
{
    release_calls_++;
    return false;
}
