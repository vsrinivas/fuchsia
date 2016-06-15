// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/handle.h>

#include <magenta/dispatcher.h>

Handle::Handle(utils::RefPtr<Dispatcher> dispatcher, uint32_t rights)
    : process_id_(0u),
      rights_(rights),
      dispatcher_(utils::move(dispatcher)),
      prev_(nullptr),
      next_(nullptr) {}

Handle::Handle(const Handle& rhs)
    : process_id_(rhs.process_id_),
      rights_(rhs.rights_),
      dispatcher_(rhs.dispatcher_),
      prev_(nullptr),
      next_(nullptr) {}

Handle::~Handle() {
    // Setting the pid to zero on destruction is critical for the correct
    // operation of the handle lookup.
    process_id_ = 0u;
    rights_ = 0u;
}

utils::RefPtr<Dispatcher> Handle::dispatcher() {
    return dispatcher_;
}

// Closes a handle. Always succeeds.
void Handle::Close() {
    dispatcher_->Close(this);
}
