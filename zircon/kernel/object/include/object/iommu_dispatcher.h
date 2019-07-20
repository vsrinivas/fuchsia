// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_IOMMU_DISPATCHER_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_IOMMU_DISPATCHER_H_

#include <sys/types.h>
#include <zircon/rights.h>

#include <dev/iommu.h>
#include <fbl/canary.h>
#include <object/dispatcher.h>
#include <object/handle.h>

class IommuDispatcher final : public SoloDispatcher<IommuDispatcher, ZX_DEFAULT_IOMMU_RIGHTS> {
 public:
  static zx_status_t Create(uint32_t type, ktl::unique_ptr<const uint8_t[]> desc, size_t desc_len,
                            KernelHandle<IommuDispatcher>* handle, zx_rights_t* rights);

  ~IommuDispatcher() final;
  zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_IOMMU; }

  fbl::RefPtr<Iommu> iommu() const { return iommu_; }

 private:
  explicit IommuDispatcher(fbl::RefPtr<Iommu> iommu);

  const fbl::RefPtr<Iommu> iommu_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_IOMMU_DISPATCHER_H_
