// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>

namespace {

#define BUILDSIG_START_MAGIC UINT64_C(0x5452545347495342) // BSIGSTRT
#define BUILDSIG_END_MAGIC UINT64_C(0x53444e4547495342) // BSIGENDS
struct buildsig {
    uint64_t start_magic;
    uint64_t buildsig_address;
    uint64_t lk_version_address;
    uint64_t note_address;
    uint64_t end_magic;
};

#define LK_VERSION_STRUCT_VERSION 0x2
struct lk_version {
    uint32_t struct_version;
    uint32_t pad;
    uint64_t arch;
    uint64_t platform;
    uint64_t target;
    uint64_t project;
    uint64_t buildid;
};

#define ELF_BUILDID_NOTE_NAME "GNU"
#define ELF_BUILDID_NOTE_NAMESZ (sizeof("GNU"))
#define ELF_BUILDID_NOTE_TYPE 3
struct elf_buildid_note {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    char name[(ELF_BUILDID_NOTE_NAMESZ + 3) & ~3];
};

class reader {
public:
    reader(FILE* input) : input_(input) {}

    bool scan() {
        pos_ = 0;
        do {
            if (consider()) {
                print();
                return true;
            }
            pos_ += 8;
        } while (fseek(input_, pos_, SEEK_SET) == 0);
        return false;
    }

private:
    FILE* input_;
    long int pos_;
    buildsig sig_;
    bool needs_byteswap_;

    static const int indent = 4;
    class extracted_item {
    public:
        extracted_item(const char* name) : name_(name) {}

        const char* name() const { return name_; }
        std::string* contents() { return &contents_; }
        const std::string* contents() const { return &contents_; }

        void print(bool last = false) const {
            printf("%*s{\"%s\": \"%s\"}%s\n",
                   indent, "", name(), contents()->c_str(),
                   last ? "" : ",");
        }

    private:
        const char* name_;
        std::string contents_;
    };

    extracted_item arch_{"arch"};
    extracted_item platform_{"platform"};
    extracted_item target_{"target"};
    extracted_item project_{"project"};
    extracted_item buildid_{"buildid"};
    extracted_item elf_buildid_{"elf_build_id"};

    void print() const {
        puts("{");
        arch_.print();
        platform_.print();
        target_.print();
        project_.print();
        buildid_.print();
        elf_buildid_.print(true);
        puts("}");
    }

    static uint64_t byteswap_constant(uint64_t constant) {
        return __builtin_bswap64(constant);
    }

    void byteswap(uint64_t* x) {
        if (needs_byteswap_)
            *x = __builtin_bswap64(*x);
    }

    void byteswap(uint32_t* x) {
        if (needs_byteswap_)
            *x = __builtin_bswap32(*x);
    }

    bool consider() {
        if (fread(&sig_, sizeof(sig_), 1, input_) == 1) {
            if (sig_.start_magic == BUILDSIG_START_MAGIC &&
                sig_.end_magic == BUILDSIG_END_MAGIC) {
                needs_byteswap_ = false;
                return decode();
            }
            if (sig_.start_magic == byteswap_constant(BUILDSIG_START_MAGIC) &&
                sig_.end_magic == byteswap_constant(BUILDSIG_END_MAGIC)) {
                needs_byteswap_ = true;
                return decode();
            }
        }
        return false;
    }

    bool decode() {
        byteswap(&sig_.buildsig_address);

        lk_version version;
        if (!read_from_address(sig_.lk_version_address,
                               &version, sizeof(version)))
            return false;
        byteswap(&version.struct_version);
        if (version.struct_version != LK_VERSION_STRUCT_VERSION)
            return false;

        return (read_string_from_address(version.arch, &arch_) &&
                read_string_from_address(version.platform, &platform_) &&
                read_string_from_address(version.target, &target_) &&
                read_string_from_address(version.project, &project_) &&
                read_string_from_address(version.buildid, &buildid_) &&
                handle_buildid_note(sig_.note_address));
    }

    bool read_from_address(uint64_t address, void* buf, size_t size) {
        return seek_to_address(address) && fread(buf, size, 1, input_) == 1;
    }

    bool read_string_from_address(uint64_t address, extracted_item* item) {
        if (seek_to_address(address)) {
            char* buf = NULL;
            size_t size = 0;
            if (getdelim(&buf, &size, '\0', input_) > 0) {
                *item->contents() = buf;
                free(buf);
                return true;
            }
        }
        return false;
    }

    bool seek_to_address(uint64_t address) {
        byteswap(&address);
        if (address > sig_.buildsig_address) {
            address -= sig_.buildsig_address;
            address += pos_;
            return (fseek(input_, address, SEEK_SET) == 0 &&
                    ftell(input_) == address);
        }
        return false;
    }

    bool handle_buildid_note(uint64_t address) {
        elf_buildid_note note;
        if (read_from_address(address, &note, sizeof(note))) {
            byteswap(&note.namesz);
            byteswap(&note.descsz);
            byteswap(&note.type);
            if (note.namesz == ELF_BUILDID_NOTE_NAMESZ &&
                note.type == ELF_BUILDID_NOTE_TYPE &&
                !memcmp(note.name, ELF_BUILDID_NOTE_NAME,
                        ELF_BUILDID_NOTE_NAMESZ)) {
                auto desc = std::make_unique<uint8_t[]>(note.descsz);
                if (fread(desc.get(), note.descsz, 1, input_) == 1) {
                    std::string* text = elf_buildid_.contents();
                    text->clear();
                    for (uint32_t i = 0; i < note.descsz; ++i) {
                        char buf[3] = "XX";
                        snprintf(buf, sizeof(buf), "%02x", desc[i]);
                        *text += buf;
                    }
                    return true;
                }
            }
        }
        return false;
    }
};

} // anonymous namespace

int main(int argc, char* argv[]) {
    const char* filename;
    FILE* input;
    switch (argc) {
    case 1:
        filename = "<standard input>";
        input = stdin;
        break;
    case 2:
        filename = argv[1];
        input = fopen(filename, "rb");
        if (!input) {
            fprintf(stderr, "%s: %s: %s\n",
                    argv[0], filename, strerror(errno));
            return 2;
        }
        break;
    default:
        fprintf(stderr, "Usage: %s [FILENAME]\n", argv[0]);
        return 1;
    }

    if (reader{input}.scan())
        return 0;

    if (ferror(input)) {
        fprintf(stderr, "%s: %s: %s\n",
                argv[0], filename, strerror(errno));
    } else {
        fprintf(stderr, "%s: %s: Cannot find a signature\n",
                argv[0], filename);
    }
    return 2;
}
