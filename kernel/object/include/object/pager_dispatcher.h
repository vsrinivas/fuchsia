// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/canary.h>
#include <fbl/ref_ptr.h>
#include <object/dispatcher.h>
#include <zircon/types.h>

class PagerDispatcher final : public SoloDispatcher<PagerDispatcher, ZX_DEFAULT_PAGER_RIGHTS> {
public:
    static zx_status_t Create(fbl::RefPtr<Dispatcher>* dispatcher, zx_rights_t* rights);

    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_PAGER; }

private:
    explicit PagerDispatcher();

    fbl::Canary<fbl::magic("PGRD")> canary_;
};
