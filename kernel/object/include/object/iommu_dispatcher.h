// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <dev/iommu.h>
#include <object/dispatcher.h>
#include <fbl/canary.h>

#include <sys/types.h>

class IommuDispatcher final : public Dispatcher {
public:
    static zx_status_t Create(uint32_t type, fbl::unique_ptr<const uint8_t[]> desc,
                              uint32_t desc_len, fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);

    ~IommuDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_IOMMU; }

    fbl::RefPtr<Iommu> iommu() const { return iommu_; }

private:
    explicit IommuDispatcher(fbl::RefPtr<Iommu> iommu);

    fbl::Canary<fbl::magic("IOMD")> canary_;
    const fbl::RefPtr<Iommu> iommu_;
};
