// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include <lib/smbios/smbios.h>

#include <fbl/algorithm.h>
#include <inttypes.h>
#include <string.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace {

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
    ZX_DEBUG_ASSERT(false);
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

} // namespace smbios
