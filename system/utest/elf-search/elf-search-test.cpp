// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf-search.h>
#include <fbl/auto_call.h>
#include <fbl/vector.h>
#include <inttypes.h>
#include <launchpad/launchpad.h>
#include <lib/zx/job.h>
#include <lib/zx/port.h>
#include <lib/zx/process.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unittest/unittest.h>
#include <zircon/status.h>
#include <zircon/syscalls/port.h>

namespace {

bool WriteHeaders(const ArrayRef<Elf64_Phdr>& phdrs, const zx::vmo& vmo) {
    BEGIN_HELPER;
    const Elf64_Ehdr ehdr = {
        .e_ident = {
            [EI_MAG0] = ELFMAG0,
            [EI_MAG1] = ELFMAG1,
            [EI_MAG2] = ELFMAG2,
            [EI_MAG3] = ELFMAG3,
            [EI_CLASS] = ELFCLASS64,
            [EI_DATA] = ELFDATA2LSB,
            [EI_VERSION] = EV_CURRENT,
            [EI_OSABI] = ELFOSABI_NONE,
        },
        .e_type = ET_DYN,
        .e_machine = kNativeElfMachine,
        .e_version = EV_CURRENT,
        .e_entry = 0,
        .e_phoff = sizeof(Elf64_Ehdr),
        .e_shoff = 0,
        .e_flags = 0,
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_phentsize = sizeof(Elf64_Phdr),
        .e_phnum = static_cast<Elf64_Half>(phdrs.size()),
        .e_shentsize = 0,
        .e_shnum = 0,
        .e_shstrndx = 0,
    };
    EXPECT_EQ(ZX_OK, vmo.write(&ehdr, 0, sizeof(ehdr)));
    EXPECT_EQ(ZX_OK, vmo.write(phdrs.begin(), sizeof(ehdr), sizeof(Elf64_Phdr) * phdrs.size()));
    END_HELPER;
}

// TODO(jakehehrlich): Switch all uses of uint8_t to std::byte once libc++ lands.

bool WriteBuildID(ArrayRef<uint8_t> build_id, const zx::vmo& vmo, uint64_t note_offset) {
    BEGIN_HELPER;
    uint8_t buf[64];
    const Elf64_Nhdr nhdr = {
        .n_namesz = sizeof(ELF_NOTE_GNU),
        .n_descsz = static_cast<Elf64_Word>(build_id.size()),
        .n_type = NT_GNU_BUILD_ID,
    };
    ASSERT_GT(sizeof(buf), sizeof(nhdr) + sizeof(ELF_NOTE_GNU) + build_id.size());
    uint64_t note_size = 0;
    memcpy(buf + note_size, &nhdr, sizeof(nhdr));
    note_size += sizeof(nhdr);
    memcpy(buf + note_size, ELF_NOTE_GNU, sizeof(ELF_NOTE_GNU));
    note_size += sizeof(ELF_NOTE_GNU);
    memcpy(buf + note_size, build_id.get(), build_id.size());
    note_size += build_id.size();
    EXPECT_EQ(ZX_OK, vmo.write(buf, note_offset, note_size));
    END_HELPER;
}

struct Module {
    fbl::StringPiece name;
    ArrayRef<Elf64_Phdr> phdrs;
    ArrayRef<uint8_t> build_id;
    zx::vmo vmo;
};

bool MakeELF(Module* mod) {
    BEGIN_HELPER;
    size_t size = 0;
    for (const auto& phdr : mod->phdrs) {
        size = fbl::max(size, phdr.p_offset + phdr.p_filesz);
    }
    ASSERT_EQ(ZX_OK, zx::vmo::create(size, 0, &mod->vmo));
    EXPECT_EQ(ZX_OK, mod->vmo.set_property(ZX_PROP_NAME, mod->name.data(), mod->name.size()));
    EXPECT_TRUE(WriteHeaders(mod->phdrs, mod->vmo));
    for (const auto& phdr : mod->phdrs) {
        if (phdr.p_type == PT_NOTE) {
            EXPECT_TRUE(WriteBuildID(mod->build_id, mod->vmo, phdr.p_offset));
        }
    }
    END_HELPER;
}

constexpr Elf64_Phdr MakePhdr(uint32_t type, uint64_t size, uint64_t addr, uint32_t flags, uint32_t align) {
    return Elf64_Phdr{
        .p_type = type,
        .p_flags = flags,
        .p_offset = addr,
        .p_vaddr = addr,
        .p_paddr = addr,
        .p_filesz = size,
        .p_memsz = size,
        .p_align = align,
    };
}

bool GetKoid(const zx::vmo& obj, zx_koid_t* out) {
    BEGIN_HELPER;
    zx_info_handle_basic_t info;
    ASSERT_EQ(ZX_OK, obj.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), NULL, NULL));
    *out = info.koid;
    END_HELPER;
}

bool ElfSearchTest() {
    BEGIN_TEST;

    // Define some dummy modules.
    constexpr Elf64_Phdr mod0_phdrs[] = {
        MakePhdr(PT_LOAD, 0x2000, 0, PF_R, 0x1000),
        MakePhdr(PT_NOTE, 20, 0x1000, PF_R, 4),
        MakePhdr(PT_LOAD, 0x1000, 0x2000, PF_R | PF_W, 0x1000),
        MakePhdr(PT_LOAD, 0x1000, 0x3000, PF_R | PF_X, 0x1000)};
    constexpr uint8_t mod0_build_id[] = {0xde, 0xad, 0xbe, 0xef};
    constexpr Elf64_Phdr mod1_phdrs[] = {
        MakePhdr(PT_LOAD, 0x2000, 0x0000, PF_R, 0x1000),
        MakePhdr(PT_NOTE, 20, 0x1000, PF_R, 4),
        MakePhdr(PT_LOAD, 0x1000, 0x2000, PF_R | PF_X, 0x1000)};
    constexpr uint8_t mod1_build_id[] = {0xff, 0xff, 0xff, 0xff};
    constexpr Elf64_Phdr mod2_phdrs[] = {
        MakePhdr(PT_LOAD, 0x2000, 0x0000, PF_R, 0x1000),
        MakePhdr(PT_NOTE, 20, 0x1000, PF_R, 4),
    };
    constexpr uint8_t mod2_build_id[] = {0x00, 0x00, 0x00, 0x00};
    Module mods[3] = {
        {"mod0", mod0_phdrs, mod0_build_id, {}},
        {"mod1", mod1_phdrs, mod1_build_id, {}},
        {"mod2", mod2_phdrs, mod2_build_id, {}},
    };

    // Load the modules and get a handle to the process.
    launchpad_t* lp;
    launchpad_create(ZX_HANDLE_INVALID, "mod-test", &lp);
    uintptr_t base, entry;
    for (auto& mod : mods) {
        EXPECT_TRUE(MakeELF(&mod));
        ASSERT_EQ(ZX_OK, launchpad_elf_load_extra(lp, mod.vmo.get(), &base, &entry), launchpad_error_message(lp));
    }
    zx::process process;
    auto ac = fbl::MakeAutoCall([&]() {
        process.kill();
    });
    EXPECT_NE(ZX_HANDLE_INVALID, *process.reset_and_get_address() = launchpad_get_process_handle(lp));

    // Now loop though everything, checking module info along the way.
    uint32_t matchCount = 0, moduleCount = 0;
    zx_status_t status = ForEachModule(process, [&](const ModuleInfo& info) {
        ++moduleCount;
        for (const auto& mod : mods) {
            if (mod.build_id == info.build_id) {
                ++matchCount;
                char name[ZX_MAX_NAME_LEN];
                zx_koid_t vmo_koid = 0;
                EXPECT_TRUE(GetKoid(mod.vmo, &vmo_koid));
                snprintf(name, sizeof(name), "<VMO#%" PRIu64 "=%s>", vmo_koid, mod.name.data());
                EXPECT_TRUE(info.name == name, "expected module names to be the same");
                EXPECT_EQ(mod.phdrs.size(), info.phdrs.size(), "expected same number of phdrs");
            }
        }
        EXPECT_EQ(moduleCount, matchCount, "Build for module was not found.");
    });
    EXPECT_EQ(ZX_OK, status, zx_status_get_string(status));
    EXPECT_EQ(moduleCount, fbl::count_of(mods), "Unexpected number of modules found.");
    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(elf_search_tests)
// TODO(jakehehrlich): Not all error cases are tested. Appropriate tests can be
// sussed out by looking at coverage results.
RUN_TEST(ElfSearchTest)
END_TEST_CASE(elf_search_tests)

int main(int argc, char** argv) {
    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
