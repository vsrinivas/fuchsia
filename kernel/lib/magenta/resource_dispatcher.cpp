// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/resource_dispatcher.h>

#include <new.h>

#include <magenta/handle.h>

constexpr mx_rights_t kDefaultResourceRights =
    MX_RIGHT_READ | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER;

status_t ResourceDispatcher::Create(mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* disp = new (&ac) ResourceDispatcher();
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultResourceRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

ResourceDispatcher::ResourceDispatcher(void) {
}

ResourceDispatcher::~ResourceDispatcher() {
}
