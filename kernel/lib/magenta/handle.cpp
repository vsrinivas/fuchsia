// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/handle.h>

#include <magenta/dispatcher.h>

Handle::Handle(mxtl::RefPtr<Dispatcher> dispatcher, uint32_t rights,
               uint32_t base_value)
    : process_id_(0u),
      dispatcher_(mxtl::move(dispatcher)),
      rights_(rights),
      base_value_(base_value) {
}

Handle::Handle(const Handle* rhs, mx_rights_t rights, uint32_t base_value)
    : process_id_(rhs->process_id_),
      dispatcher_(rhs->dispatcher_),
      rights_(rights),
      base_value_(base_value) {
}

mxtl::RefPtr<Dispatcher> Handle::dispatcher() const { return dispatcher_; }
