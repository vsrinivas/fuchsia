// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include <platform/pc/smbios.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <lib/console.h>
#include <platform/pc/bootloader.h>
#include <stdint.h>
#include <string.h>
#include <vm/physmap.h>
#include <vm/vm_aspace.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object_physical.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#define SMBIOS2_ANCHOR "_SM_"
#define SMBIOS2_INTERMEDIATE_ANCHOR "_DMI_"
#define SMBIOS3_ANCHOR "_SM3_"

namespace {

smbios::EntryPointVersion kEpVersion = smbios::EntryPointVersion::Unknown;
union {
    const uint8_t* raw;
    const smbios::EntryPoint2_1* ep2_1;
} kEntryPoint;
uintptr_t kStructBase = 0; // Address of first SMBIOS struct

uint8_t ComputeChecksum(const uint8_t* data, size_t len) {
    unsigned int sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return static_cast<uint8_t>(sum);
}

} // namespace


namespace smbios {

StringTable::StringTable() {
}
StringTable::~StringTable() {
}

zx_status_t StringTable::Init(const Header* h, size_t max_struct_len) {
    if (h->length > max_struct_len) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    size_t max_string_table_len = max_struct_len - h->length;
    start_ = reinterpret_cast<const char*>(h) + h->length;

    // Make sure the table is big enough to include the two trailing NULs
    if (max_string_table_len < 2) {
        return ZX_ERR_IO_DATA_INTEGRITY;
    }

    // Check if the string table is empty
    if (start_[0] == 0 && start_[1] == 0) {
        length_ = 2;
        return ZX_OK;
    }

    size_t start_idx = 0;
    if (start_[0] == 0) {
        // We know that this isn't the end of the table, since the next byte
        // isn't NUL.  Skip examining this leading zero-length string in the
        // loop below, so that we can simplify the iteration.  During the
        // iteration below, we have the invariant that either
        // 1) i points to the start of the first string in the table and that
        // string is not 0-length
        // 2) i points to a subsequent string in the table, so a zero-length
        // string implies two consecutive NULs were found (the end of table
        // marker).
        start_idx = 1;
    }

    for (size_t i = start_idx; i < max_string_table_len; ) {
        size_t len = strnlen(start_ + i, max_string_table_len - i);

        if (len == 0) {
            length_ = i + 1; // Include the trailing null
            return ZX_OK;
        }

        // strnlen returns the length not including the NUL.  Note that if
        // no NUL was found, it returns max_string_table_len - i, which will exceed
        // the loop conditions.
        i += len + 1;
    }
    return ZX_ERR_IO_DATA_INTEGRITY;
}

zx_status_t StringTable::GetString(size_t idx, const char** out) const {
    if (idx == 0) {
        *out = "<null>";
        return ZX_OK;
    }
    *out = "<missing string>";

    for (size_t i = 0; i < length_; ) {
        size_t len = strnlen(start_ + i, length_ - i);

        if (len == 0) {
            if (i != 0) {
                return ZX_ERR_NOT_FOUND;
            }

            if (length_ - i < 2) {
                return ZX_ERR_IO_DATA_INTEGRITY;
            }
            if (start_[i + 1] == 0) {
                return ZX_ERR_NOT_FOUND;
            }
        }
        if (idx == 1) {
            *out = start_ + i;
            return ZX_OK;
        }
        idx--;
        i += len + 1;
    }
    DEBUG_ASSERT(false);
    // Should not be reachable, since Init should have checked
    return ZX_ERR_IO_DATA_INTEGRITY;
}

void StringTable::Dump() const {
    const char* str;
    for (size_t i = 1; GetString(i, &str) == ZX_OK; ++i) {
        printf("  str %zu: %s\n", i, str);
    }
}

bool EntryPoint2_1::IsValid() const {
    if (memcmp(anchor_string, SMBIOS2_ANCHOR, fbl::count_of(anchor_string))) {
        printf("smbios: bad anchor %4s\n", anchor_string);
        return false;
    }

    uint8_t real_length = length;
    if (length != 0x1f) {
        // 0x1e is allowed due to errata in the SMBIOS 2.1 spec.  It really means
        // 0x1f.
        if (length == 0x1e) {
            real_length = 0x1f;
        } else {
            printf("smbios: bad len: %u\n", real_length);
            return false;
        }
    }

    if (ComputeChecksum(reinterpret_cast<const uint8_t*>(this), real_length) != 0) {
        printf("smbios: bad checksum\n");
        return false;
    }
    if (ep_rev != 0) {
        printf("smbios: bad version %u\n", ep_rev);
        return false;
    }

    if (memcmp(intermediate_anchor_string, SMBIOS2_INTERMEDIATE_ANCHOR,
               fbl::count_of(intermediate_anchor_string))) {
        printf("smbios: bad intermediate anchor %5s\n", intermediate_anchor_string);
        return false;
    }
    if (ComputeChecksum(reinterpret_cast<const uint8_t*>(&intermediate_anchor_string),
                        real_length - offsetof(EntryPoint2_1, intermediate_anchor_string)) != 0) {
        printf("smbios: bad intermediate checksum\n");
        return false;
    }

    if ((uint32_t)(struct_table_phys + struct_table_length) < struct_table_phys) {
        return false;
    }

    return true;
}

void EntryPoint2_1::Dump() const {
    printf("SMBIOS EntryPoint v2.1:\n");
    printf("  specification version: %u.%u\n", major_ver, minor_ver);
    printf("  max struct size: %u\n", max_struct_size);
    printf("  struct table: %u bytes @0x%08x, %u entries\n", struct_table_length, struct_table_phys,
           struct_count);
}

bool SpecVersion::IncludesVersion(uint8_t spec_major_ver, uint8_t spec_minor_ver) const {
    if (major_ver > spec_major_ver) {
        return true;
    }
    if (major_ver < spec_major_ver) {
        return false;
    }
    return minor_ver >= spec_minor_ver;
}

void BiosInformationStruct2_0::Dump(const StringTable& st) const {
    printf("SMBIOS BIOS Information Struct v2.0:\n");
    printf("  vendor: %s\n", st.GetString(vendor_str_idx));
    printf("  BIOS version: %s\n", st.GetString(bios_version_str_idx));
    printf("  BIOS starting address segment: 0x%04x\n", bios_starting_address_segment);
    printf("  BIOS release date: %s\n", st.GetString(bios_release_date_str_idx));
    printf("  BIOS ROM size: 0x%02x\n", bios_rom_size);
    printf("  BIOS characteristics: 0x%016" PRIx64 "\n", bios_characteristics);
    for (size_t i = sizeof(*this); i < hdr.length; ++i) {
        printf("  BIOS characteristics extended: 0x%02x\n", bios_characteristics_ext[i]);
    }
}

void BiosInformationStruct2_4::Dump(const StringTable& st) const {
    printf("SMBIOS BIOS Information Struct v2.4:\n");
    printf("  vendor: %s\n", st.GetString(vendor_str_idx));
    printf("  BIOS version: %s\n", st.GetString(bios_version_str_idx));
    printf("  BIOS starting address segment: 0x%04x\n", bios_starting_address_segment);
    printf("  BIOS release date: %s\n", st.GetString(bios_release_date_str_idx));
    printf("  BIOS ROM size: 0x%02x\n", bios_rom_size);
    printf("  BIOS characteristics: 0x%016" PRIx64 "\n", bios_characteristics);
    printf("  BIOS characteristics extended: 0x%04x\n", bios_characteristics_ext);
    printf("  BIOS version number: %u.%u\n", bios_major_release, bios_minor_release);
    printf("  EC version number: %u.%u\n", ec_major_release, ec_minor_release);
    if (hdr.length > sizeof(*this)) {
        printf("  %zu bytes of unknown trailing contents\n", hdr.length - sizeof(*this));
    }
}

void SystemInformationStruct2_0::Dump(const StringTable& st) const {
    printf("SMBIOS System Information Struct v2.0:\n");
    printf("  manufacturer: %s\n", st.GetString(manufacturer_str_idx));
    printf("  product: %s\n", st.GetString(product_name_str_idx));
    printf("  version: %s\n", st.GetString(version_str_idx));
    if (hdr.length > sizeof(*this)) {
        printf("  %zu bytes of unknown trailing contents\n", hdr.length - sizeof(*this));
    }
}

void SystemInformationStruct2_1::Dump(const StringTable& st) const {
    printf("SMBIOS System Information Struct v2.1:\n");
    printf("  manufacturer: %s\n", st.GetString(manufacturer_str_idx));
    printf("  product: %s\n", st.GetString(product_name_str_idx));
    printf("  version: %s\n", st.GetString(version_str_idx));
    printf("  wakeup_type: 0x%x\n", wakeup_type);
    if (hdr.length > sizeof(*this)) {
        printf("  %zu bytes of unknown trailing contents\n", hdr.length - sizeof(*this));
    }
}

void SystemInformationStruct2_4::Dump(const StringTable& st) const {
    printf("SMBIOS System Information Struct v2.4:\n");
    printf("  manufacturer: %s\n", st.GetString(manufacturer_str_idx));
    printf("  product: %s\n", st.GetString(product_name_str_idx));
    printf("  version: %s\n", st.GetString(version_str_idx));
    printf("  wakeup_type: 0x%x\n", wakeup_type);
    printf("  SKU: %s\n", st.GetString(sku_number_str_idx));
    printf("  family: %s\n", st.GetString(family_str_idx));
    if (hdr.length > sizeof(*this)) {
        printf("  %zu bytes of unknown trailing contents\n", hdr.length - sizeof(*this));
    }
}

zx_status_t EntryPoint2_1::WalkStructs(uintptr_t struct_table_virt, StructWalkCallback cb,
                                       void* ctx) const {
    size_t idx = 0;
    uintptr_t curr_addr = struct_table_virt;
    const uintptr_t table_end = curr_addr + struct_table_length;
    while (curr_addr + sizeof(Header) < table_end) {
        auto hdr = reinterpret_cast<const Header*>(curr_addr);
        if (curr_addr + hdr->length > table_end) {
            return ZX_ERR_IO_DATA_INTEGRITY;
        }
        StringTable st;
        zx_status_t status = st.Init(hdr, fbl::max(table_end - curr_addr,
                                                   static_cast<size_t>(max_struct_size)));
        if (status != ZX_OK) {
            return status;
        }

        status = cb(version(), hdr, st, ctx);
        if (status == ZX_ERR_STOP) {
            break;
        } else if (status != ZX_OK && status != ZX_ERR_NEXT) {
            return status;
        }
        idx++;

        if (idx == struct_count) {
            return ZX_OK;
        }

        // Skip over the embedded strings
        curr_addr += hdr->length + st.length();
    }

    return ZX_ERR_IO_DATA_INTEGRITY;
}

// Walk the known SMBIOS structures.  The callback will be called once for each
// structure found.
zx_status_t WalkStructs(StructWalkCallback cb, void* ctx) {
    switch (kEpVersion) {
        case smbios::EntryPointVersion::V2_1: {
            return kEntryPoint.ep2_1->WalkStructs(kStructBase, cb, ctx);
        }
        case smbios::EntryPointVersion::V3_0:
            return ZX_ERR_NOT_SUPPORTED;
        default:
            return ZX_ERR_NOT_SUPPORTED;
    }
}

} // namespace smbios

namespace {

zx_status_t FindEntryPoint(const uint8_t** base, smbios::EntryPointVersion* version) {
    // See if our bootloader told us where the table is
    if (bootloader.smbios != 0) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(paddr_to_physmap(bootloader.smbios));
        if (!memcmp(p, SMBIOS2_ANCHOR, strlen(SMBIOS2_ANCHOR))) {
            *base = p;
            *version = smbios::EntryPointVersion::V2_1;
            return ZX_OK;
        } else if (!memcmp(p, SMBIOS3_ANCHOR, strlen(SMBIOS3_ANCHOR))) {
            *base = p;
            *version = smbios::EntryPointVersion::V3_0;
            return ZX_OK;
        }
    }

    // Fallback to non-EFI SMBIOS search if we haven't found it yet
    for (paddr_t target = 0x000f0000; target < 0x00100000; target += 16) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(paddr_to_physmap(target));
        if (!memcmp(p, SMBIOS2_ANCHOR, strlen(SMBIOS2_ANCHOR))) {
            *base = p;
            *version = smbios::EntryPointVersion::V2_1;
            return ZX_OK;
        }
        if (!memcmp(p, SMBIOS3_ANCHOR, strlen(SMBIOS3_ANCHOR))) {
            *base = p;
            *version = smbios::EntryPointVersion::V3_0;
            return ZX_OK;
        }
    }

    return ZX_ERR_NOT_FOUND;
}

zx_status_t MapStructs2_1(const smbios::EntryPoint2_1* ep,
                          fbl::RefPtr<VmMapping>* mapping, uintptr_t* struct_table_virt) {
    paddr_t base = ep->struct_table_phys;
    paddr_t end = base + ep->struct_table_length;
    const size_t subpage_offset = base & (PAGE_SIZE - 1);
    base -= subpage_offset;
    size_t len = ROUNDUP(end - base, PAGE_SIZE);

    auto vmar = VmAspace::kernel_aspace()->RootVmar();
    fbl::RefPtr<VmObject> vmo;
    zx_status_t status = VmObjectPhysical::Create(base, len, &vmo);
    if (status != ZX_OK) {
        return status;
    }
    fbl::RefPtr<VmMapping> m;
    status = vmar->CreateVmMapping(0, len, 0, 0 /* vmar_flags */, fbl::move(vmo), 0,
                                   ARCH_MMU_FLAG_CACHED | ARCH_MMU_FLAG_PERM_READ,
                                   "smbios", &m);
    if (status != ZX_OK) {
        return status;
    }
    *struct_table_virt = m->base() + subpage_offset;
    *mapping = fbl::move(m);
    return ZX_OK;
}

} // namespace

void pc_init_smbios() {
    fbl::RefPtr<VmMapping> mapping;
    auto cleanup_mapping = fbl::MakeAutoCall([&mapping] {
        if (mapping) {
            mapping->Destroy();
        }
    });

    const uint8_t* start = nullptr;
    auto version = smbios::EntryPointVersion::Unknown;
    uintptr_t struct_table_virt = 0;

    zx_status_t status = FindEntryPoint(&start, &version);
    if (status != ZX_OK) {
        printf("smbios: Failed to locate entry point\n");
        return;
    }

    switch (version) {
        case smbios::EntryPointVersion::V2_1: {
            auto ep = reinterpret_cast<const smbios::EntryPoint2_1*>(start);
            if (!ep->IsValid()) {
                return;
            }

            status = MapStructs2_1(ep, &mapping, &struct_table_virt);
            if (status != ZX_OK) {
                printf("smbios: failed to map structs: %d\n", status);
                return;
            }
            break;
        }
        case smbios::EntryPointVersion::V3_0:
            printf("smbios: version 3 not yet implemented\n");
            return;
        default:
            DEBUG_ASSERT(false);
            printf("smbios: Unknown version?\n");
            return;
    }

    kEntryPoint.raw = start;
    kEpVersion = version;
    kStructBase = struct_table_virt;
    cleanup_mapping.cancel();
}

static zx_status_t DebugStructWalk(smbios::SpecVersion ver,
                                   const smbios::Header* hdr, const smbios::StringTable& st,
                                   void* ctx) {
    switch (hdr->type) {
        case 0: {
            if (ver.IncludesVersion(2, 4)) {
                auto entry = reinterpret_cast<const smbios::BiosInformationStruct2_4*>(hdr);
                entry->Dump(st);
                return ZX_OK;
            } else if (ver.IncludesVersion(2, 0))  {
                auto entry = reinterpret_cast<const smbios::BiosInformationStruct2_0*>(hdr);
                entry->Dump(st);
                return ZX_OK;
            }
            break;
        }
        case 1: {
            if (ver.IncludesVersion(2, 4)) {
                auto entry = reinterpret_cast<const smbios::SystemInformationStruct2_4*>(hdr);
                entry->Dump(st);
                return ZX_OK;
            } else if (ver.IncludesVersion(2, 1))  {
                auto entry = reinterpret_cast<const smbios::SystemInformationStruct2_1*>(hdr);
                entry->Dump(st);
                return ZX_OK;
            } else if (ver.IncludesVersion(2, 0))  {
                auto entry = reinterpret_cast<const smbios::SystemInformationStruct2_0*>(hdr);
                entry->Dump(st);
                return ZX_OK;
            }
            break;
        }
    }
    printf("smbios: found struct@%p: typ=%u len=%u st_len=%zu\n", hdr, hdr->type, hdr->length, st.length());
    st.Dump();

    return ZX_OK;
}

static int CmdSmbios(int argc, const cmd_args *argv, uint32_t flags)
{
    if (argc < 2) {
        printf("not enough arguments\n");
usage:
        printf("usage:\n");
        printf("%s dump\n", argv[0].str);
        return ZX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "dump")) {
        zx_status_t status = smbios::WalkStructs(DebugStructWalk, nullptr);
        if (status != ZX_OK) {
            printf("smbios: failed to walk structs: %d\n", status);
        }
        return ZX_OK;
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return ZX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("smbios", "smbios", &CmdSmbios)
#endif
STATIC_COMMAND_END(smbios);
