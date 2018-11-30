// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/pager_dispatcher.h>

zx_status_t PagerDispatcher::Create(fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) PagerDispatcher();
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

PagerDispatcher::PagerDispatcher() : SoloDispatcher() {}
