// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/rodso.h>
#include <magenta/process_dispatcher.h>

class VDso : public RoDso {
public:
    VDso();

    inline mx_status_t Map(mxtl::RefPtr<ProcessDispatcher> process,
                           uintptr_t vdso_base) {
        return MapFixed(mxtl::move(process), vdso_base);
    }
};
