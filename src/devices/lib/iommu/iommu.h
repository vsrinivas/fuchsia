// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_IOMMU_IOMMU_H_
#define SRC_DEVICES_LIB_IOMMU_IOMMU_H_

#include <lib/fit/function.h>
#include <lib/syslog/logger.h>
#include <lib/zx/iommu.h>
#include <zircon/syscalls/iommu.h>

#include <acpica/acpi.h>
#include <fbl/array.h>
#include <fbl/mutex.h>
#include <fbl/span.h>

namespace x86 {

class IommuTest;

// Helper class for the IommuManager that represents a dmar unit and its descriptor.
class IommuDesc {
 public:
  IommuDesc() = default;
  ~IommuDesc() = default;

  zx_status_t CreatePartialSegmentDesc(const ACPI_TABLE_DMAR* table,
                                       const ACPI_DMAR_HARDWARE_UNIT* unit);

  zx_status_t CreateWholeSegmentDesc(const ACPI_TABLE_DMAR* table,
                                     const ACPI_DMAR_HARDWARE_UNIT* unit);

  // Creates the zircon iommu object using the supplied root resource. Only valid to be called after
  // one of the Create*Desc initializers has returned with ZX_OK.
  zx_status_t CreateIommu(const zx::unowned_resource& root_resource);

  // Retrieves the zircon iommu object. Only valid to be called after CreateIommu has returned with
  // ZX_OK.
  zx::unowned_iommu GetIommu() { return zx::unowned_iommu(iommu_); }

  // Returns a reference to the descriptor header. Only valid to be called after one of the
  // Create*Desc initializers has returned with ZX_OK.
  const zx_iommu_desc_intel_t& Desc() const {
    return *reinterpret_cast<zx_iommu_desc_intel_t*>(desc_.get());
  }

  // Returns a Span of the scopes in the descriptor data. Only valid to be called after one of the
  // Create*Desc initializers has returned with ZX_OK.
  fbl::Span<zx_iommu_desc_intel_scope_t> Scopes() {
    // The scopes live just after the desc header.
    return {
        reinterpret_cast<zx_iommu_desc_intel_scope_t*>(desc_.get() + sizeof(zx_iommu_desc_intel_t)),
        Desc().scope_bytes / sizeof(zx_iommu_desc_intel_scope_t)};
  }

 private:
  // Give the unit test code access.
  friend IommuTest;

  template <typename F>
  zx_status_t CreateDesc(const ACPI_TABLE_DMAR* table, uint64_t base, uint16_t pci_segment,
                         bool whole_segment, F scope_func);

  template <typename F>
  zx_status_t AllocDesc(const ACPI_TABLE_DMAR* table, uint16_t pci_segment, bool whole_segment,
                        F scope_func);

  fbl::Span<uint8_t> ReservedMem() {
    // The reserved memory information starts at the end of the scopes.
    return {reinterpret_cast<uint8_t*>(Scopes().end()), Desc().reserved_memory_bytes};
  }
  zx_iommu_desc_intel_t* RawDesc() { return reinterpret_cast<zx_iommu_desc_intel_t*>(desc_.get()); }

  // Memory allocation of the descriptor. The first thing in the allocation is a
  // zx_iommu_desc_intel_t, but there is a variety of data immediately following it.
  fbl::Array<uint8_t> desc_;

  zx::iommu iommu_;
};

using IommuLogger = fit::function<void(fx_log_severity_t severity, const char* file, int line,
                                       const char* msg, va_list args)>;

// Internally synchronized iommu manager.
class IommuManager {
 public:
  explicit IommuManager(IommuLogger logger);
  ~IommuManager();

  // Initializes the iommu_manager using the ACPI DMAR table. If this fails,
  // the IOMMU manager will be left in a well-defined empty state, and
  // IommuForBdf() can still succeed (yielding dummy IOMMU handles).
  zx_status_t Init(zx::unowned_resource root_resource, bool force_hardware_iommu);

  // Returns a handle to the IOMMU that is responsible for the given BDF.
  zx::unowned_iommu IommuForBdf(uint32_t bdf);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(IommuManager);

  // Initializes the iommus_ from the given DMAR. This does not call IommuDesc::CreateIommu and is
  // suitable for hooking into from unit test code.
  zx_status_t InitDesc(const ACPI_TABLE_DMAR* dmar);

  void Logf(fx_log_severity_t severity, const char* file, int line, const char* msg, ...)
      __PRINTFLIKE(5, 6);

  IommuLogger logger_;

  fbl::Mutex lock_;

  // Array of IOMMUs.
  fbl::Array<IommuDesc> iommus_;

  // Used for BDFs not covered by the ACPI tables.
  zx::iommu dummy_iommu_;

  // Give the unit test code access.
  friend IommuTest;
};

}  // namespace x86

// Returns a handle to the IOMMU that is responsible for the given BDF. The
// returned handle is borrowed from the iommu_manager.  The caller
// must not close the handle.
zx_status_t iommu_manager_iommu_for_bdf(uint32_t bdf, zx_handle_t* iommu);

#endif  // SRC_DEVICES_LIB_IOMMU_IOMMU_H_
