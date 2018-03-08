// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>

#include "hw.h"
#include "iommu_page.h"

namespace intel_iommu {

class DeviceContext;
class IommuImpl;

class ContextTableState : public fbl::DoublyLinkedListable<fbl::unique_ptr<ContextTableState>> {
public:
    ~ContextTableState();

    // Create a ContextTableState for the given bus.
    // If |extended| is true, then this will represent a reg::ExtendedContextTable,
    // and the table will handle translations for, depending on |upper|, either the
    // lower (dev<16) or upper half of this bus.
    // If |extended| is false, this represents a reg::ContextTable.
    static zx_status_t Create(uint8_t bus, bool extended, bool upper,
                              IommuImpl* parent, volatile ds::RootEntrySubentry* root_entry,
                              fbl::unique_ptr<ContextTableState>* table);

    // Check if this ContextTableState is for the given BDF
    bool includes_bdf(ds::Bdf bdf) const {
        if (bdf.bus() != bus_) {
            return false;
        }
        if (!extended_) {
            return true;
        }
        return (bdf.dev() >= 16) == upper_;
    }

    // Create a new DeviceContext representing the given BDF, and give it the specified domain_id.
    // It is a fatal error to try to create a context for a BDF that already has one.
    zx_status_t CreateDeviceContext(ds::Bdf bdf, uint32_t domain_id,
                                    DeviceContext** context);

    zx_status_t GetDeviceContext(ds::Bdf bdf, DeviceContext** context);

private:
    ContextTableState(uint8_t bus, bool extended, bool upper, IommuImpl* parent,
                      volatile ds::RootEntrySubentry* root_entry, IommuPage page);

    DISALLOW_COPY_ASSIGN_AND_MOVE(ContextTableState);

    volatile ds::ContextTable* table() const {
        DEBUG_ASSERT(!extended_);
        return reinterpret_cast<volatile ds::ContextTable*>(page_.vaddr());
    }

    volatile ds::ExtendedContextTable* extended_table() const {
        DEBUG_ASSERT(extended_);
        return reinterpret_cast<volatile ds::ExtendedContextTable*>(page_.vaddr());
    }

    // Pointer to IOMMU that owns this ContextTableState
    IommuImpl* const parent_;
    // Pointer to the half of the Root Table Entry that decodes to this
    // ContextTable.
    volatile ds::RootEntrySubentry* const root_entry_;

    // Page backing the ContextTable/ExtendedContextTable
    const IommuPage page_;

    // List of device configurations beneath this ContextTable.
    fbl::DoublyLinkedList<fbl::unique_ptr<DeviceContext>> devices_;

    const uint8_t bus_;
    const bool extended_;
    // Only valid if extended_ is true
    const bool upper_;
};

} // namespace intel_iommu
