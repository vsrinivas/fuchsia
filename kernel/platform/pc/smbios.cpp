// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
//
#include <platform/pc/smbios.h>

#include <fbl/auto_call.h>
#include <lib/console.h>
#include <lib/smbios/smbios.h>
#include <platform/pc/bootloader.h>
#include <stdint.h>
#include <string.h>
#include <vm/physmap.h>
#include <vm/vm_aspace.h>
#include <vm/vm_address_region.h>
#include <vm/vm_object_physical.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

namespace {

smbios::EntryPointVersion kEpVersion = smbios::EntryPointVersion::Unknown;
union {
    const uint8_t* raw;
    const smbios::EntryPoint2_1* ep2_1;
} kEntryPoint;
uintptr_t kStructBase = 0; // Address of first SMBIOS struct

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

// Walk the known SMBIOS structures.  The callback will be called once for each
// structure found.
zx_status_t SmbiosWalkStructs(smbios::StructWalkCallback cb, void* ctx) {
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
        case smbios::StructType::BiosInfo: {
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
        case smbios::StructType::SystemInfo: {
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
        default: break;
    }
    printf("smbios: found struct@%p: typ=%u len=%u st_len=%zu\n", hdr,
           static_cast<uint8_t>(hdr->type), hdr->length, st.length());
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
        zx_status_t status = SmbiosWalkStructs(DebugStructWalk, nullptr);
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
