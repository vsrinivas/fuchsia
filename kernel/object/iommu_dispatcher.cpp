// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/iommu_dispatcher.h>

#include <dev/iommu.h>
#include <zircon/rights.h>
#include <zxcpp/new.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

zx_status_t IommuDispatcher::Create(uint32_t type, fbl::unique_ptr<const uint8_t[]> desc,
                                    uint32_t desc_len, fbl::RefPtr<Dispatcher>* dispatcher,
                                    zx_rights_t* rights) {

    fbl::RefPtr<Iommu> iommu;
    switch (type) {
        default:
            return ZX_ERR_NOT_SUPPORTED;
    }

    fbl::AllocChecker ac;
    auto disp = new (&ac) IommuDispatcher(fbl::move(iommu));
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = ZX_DEFAULT_IOMMU_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

IommuDispatcher::IommuDispatcher(fbl::RefPtr<Iommu> iommu)
    : iommu_(iommu) {}

IommuDispatcher::~IommuDispatcher() {
}

