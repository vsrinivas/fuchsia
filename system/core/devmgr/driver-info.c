// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ddk/binding.h>

typedef struct {
    uint32_t namesz;
    uint32_t descsz;
    uint32_t type;
    char name[0];
} notehdr;

typedef Elf64_Ehdr elfhdr;
typedef Elf64_Phdr elfphdr;

static mx_status_t find_note(const char* name, uint32_t type,
                             void* data, size_t size,
                             mx_status_t (*func)(void* note, size_t sz, void* cookie),
                             void* cookie) {
    size_t nlen = strlen(name);
    while (size >= sizeof(notehdr)) {
        // ignore padding between notes
        if (*((uint32_t*) data) == 0) {
            size -= sizeof(uint32_t);
            data += sizeof(uint32_t);
            continue;
        }
        notehdr* hdr = data;
        uint32_t nsz = (hdr->namesz + 3) & (~3);
        if (nsz > (size - sizeof(notehdr))) {
            return ERR_INTERNAL;
        }
        data += sizeof(notehdr) + nsz;
        size -= sizeof(notehdr) + nsz;

        uint32_t dsz = (hdr->descsz + 3) & (~3);
        if (dsz > size) {
            return ERR_INTERNAL;
        }

        if ((hdr->type == type) &&
            (hdr->namesz == nlen) &&
            (memcmp(name, hdr->name, nlen) == 0)) {
            return func(data, hdr->descsz, cookie);
        }

        data += dsz;
        size -= dsz;
    }
    return ERR_NOT_FOUND;
}

static mx_status_t for_each_note(int fd, const char* name, uint32_t type,
                                 void* data, size_t dsize,
                                 mx_status_t (*func)(void* note, size_t sz, void* cookie),
                                 void* cookie) {
    elfphdr ph[64];
    elfhdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh)) {
        printf("for_each_note: pread(eh) failed\n");
        return ERR_IO;
    }
    if (memcmp(&eh, ELFMAG, 4) ||
        (eh.e_ehsize != sizeof(elfhdr)) ||
        (eh.e_phentsize != sizeof(elfphdr))) {
        printf("for_each_note: bad elf magic\n");
        return ERR_INTERNAL;
    }
    size_t sz = sizeof(elfphdr) * eh.e_phnum;
    if (sz > sizeof(ph)) {
        printf("for_each_note: too many phdrs\n");
        return ERR_INTERNAL;
    }
    if ((pread(fd, ph, sz, eh.e_phoff) != (ssize_t)sz)) {
        printf("for_each_note: pread(sz,eh.e_phoff) failed\n");
        return ERR_IO;
    }
    for (int i = 0; i < eh.e_phnum; i++) {
        if ((ph[i].p_type != PT_NOTE) ||
            (ph[i].p_filesz > dsize)) {
            continue;
        }
        if ((pread(fd, data, ph[i].p_filesz, ph[i].p_offset) != (ssize_t)ph[i].p_filesz)) {
            printf("for_each_note: pread(ph[i]) failed\n");
            return ERR_IO;
        }
        int r = find_note(name, type, data, ph[i].p_filesz, func, cookie);
        if (r == NO_ERROR) {
            return r;
        }
    }
    return ERR_NOT_FOUND;
}

typedef struct {
    magenta_note_driver_t drv;
    mx_bind_inst_t bi[0];
} drivernote;

typedef struct {
    void* cookie;
    void (*func)(magenta_note_driver_t*, mx_bind_inst_t*, void*);
} context;

static mx_status_t callback(void* note, size_t sz, void* _ctx) {
    context* ctx = _ctx;
    drivernote* dn = note;
    if (sz < sizeof(drivernote)) {
        return ERR_INTERNAL;
    }
    size_t max = (sz - sizeof(drivernote)) / sizeof(mx_bind_inst_t);
    if (dn->drv.bindcount > max) {
        return ERR_INTERNAL;
    }
    ctx->func(&dn->drv, dn->bi, ctx->cookie);
    return NO_ERROR;
}

mx_status_t read_driver_info(int fd, void *cookie,
                             void (*func)(magenta_note_driver_t* note,
                                          mx_bind_inst_t* binding,
                                          void *cookie)) {
    context ctx = {
        .cookie = cookie,
        .func = func,
    };
    uint8_t data[4096];
    return for_each_note(fd, "Magenta", MAGENTA_NOTE_DRIVER,
                         data, sizeof(data), callback, &ctx);
}

const char* lookup_bind_param_name(uint32_t param_num) {
    switch (param_num) {
        case BIND_FLAGS:                  return "P.Flags";
        case BIND_PROTOCOL:               return "P.Protocol";
        case BIND_AUTOBIND:               return "P.Autobind";
        case BIND_PCI_VID:                return "P.PCI.VID";
        case BIND_PCI_DID:                return "P.PCI.DID";
        case BIND_PCI_CLASS:              return "P.PCI.Class";
        case BIND_PCI_SUBCLASS:           return "P.PCI.Subclass";
        case BIND_PCI_INTERFACE:          return "P.PCI.Interface";
        case BIND_PCI_REVISION:           return "P.PCI.Revision";
        case BIND_PCI_BDF_ADDR:           return "P.PCI.BDFAddr";
        case BIND_USB_VID:                return "P.USB.VID";
        case BIND_USB_PID:                return "P.USB.PID";
        case BIND_USB_CLASS:              return "P.USB.Class";
        case BIND_USB_SUBCLASS:           return "P.USB.Subclass";
        case BIND_USB_PROTOCOL:           return "P.USB.Protocol";
        case BIND_PLATFORM_DEV_VID:       return "P.PlatDev.VID";
        case BIND_PLATFORM_DEV_PID:       return "P.PlatDev.PID";
        case BIND_PLATFORM_DEV_DID:       return "P.PlatDev.DID";
        case BIND_ACPI_HID_0_3:           return "P.ACPI.HID[0-3]";
        case BIND_ACPI_HID_4_7:           return "P.ACPI.HID[4-7]";
        case BIND_IHDA_CODEC_VID:         return "P.IHDA.Codec.VID";
        case BIND_IHDA_CODEC_DID:         return "P.IHDA.Codec.DID";
        case BIND_IHDA_CODEC_MAJOR_REV:   return "P.IHDACodec.MajorRev";
        case BIND_IHDA_CODEC_MINOR_REV:   return "P.IHDACodec.MinorRev";
        case BIND_IHDA_CODEC_VENDOR_REV:  return "P.IHDACodec.VendorRev";
        case BIND_IHDA_CODEC_VENDOR_STEP: return "P.IHDACodec.VendorStep";
        default: return NULL;
    }
}

void dump_bind_inst(const mx_bind_inst_t* b, char* buf, size_t buf_len) {
    if (!b || !buf || !buf_len) {
        return;
    }

    uint32_t cc = BINDINST_CC(b->op);
    uint32_t op = BINDINST_OP(b->op);
    uint32_t pa = BINDINST_PA(b->op);
    uint32_t pb = BINDINST_PB(b->op);
    size_t off = 0;
    buf[0] = 0;

    switch (op) {
    case OP_ABORT:
    case OP_MATCH:
    case OP_GOTO:
    case OP_SET:
    case OP_CLEAR:
        break;
    case OP_LABEL:
        snprintf(buf + off, buf_len - off, "L.%u:\n", b->arg);
        return;
    default:
        snprintf(buf + off, buf_len - off,
                "Unknown Op 0x%1x [0x%08x, 0x%08x]\n", op, b->op, b->arg);
        return;
    }

    off += snprintf(buf + off, buf_len - off, "if (");
    if (cc == COND_AL) {
        off += snprintf(buf + off, buf_len - off, "true");
    } else {
        const char* pb_name = lookup_bind_param_name(pb);
        if (pb_name) {
            off += snprintf(buf + off, buf_len - off, "%s", pb_name);
        } else {
            off += snprintf(buf + off, buf_len - off, "P.%04x", pb);
        }

        switch (cc) {
        case COND_EQ:
            off += snprintf(buf + off, buf_len - off, " == 0x%08x", b->arg);
            break;
        case COND_NE:
            off += snprintf(buf + off, buf_len - off, " != 0x%08x", b->arg);
            break;
        case COND_GT:
            off += snprintf(buf + off, buf_len - off, " > 0x%08x", b->arg);
            break;
        case COND_LT:
            off += snprintf(buf + off, buf_len - off, " < 0x%08x", b->arg);
            break;
        case COND_GE:
            off += snprintf(buf + off, buf_len - off, " >= 0x%08x", b->arg);
            break;
        case COND_LE:
            off += snprintf(buf + off, buf_len - off, " <= 0x%08x", b->arg);
            break;
        case COND_MASK:
            off += snprintf(buf + off, buf_len - off, " & 0x%08x != 0", b->arg);
            break;
        case COND_BITS:
            off += snprintf(buf + off, buf_len - off, " & 0x%08x == 0x%08x", b->arg, b->arg);
            break;
        default:
            off += snprintf(buf + off, buf_len - off,
                            " ?(0x%x) 0x%08x", cc, b->arg);
            break;
        }
    }
    off += snprintf(buf + off, buf_len - off, ") ");

    switch (op) {
    case OP_ABORT:
        off += snprintf(buf + off, buf_len - off, "return no-match;");
        break;
    case OP_MATCH:
        off += snprintf(buf + off, buf_len - off, "return match;");
        break;
    case OP_GOTO:
        off += snprintf(buf + off, buf_len - off, "goto L.%u;", b->arg);
        break;
    case OP_SET:
        off += snprintf(buf + off, buf_len - off, "flags |= 0x%02x;", pa);
        break;
    case OP_CLEAR:
        off += snprintf(buf + off, buf_len - off, "flags &= 0x%02x;", ~pa & 0xFF);
        break;
    }
}
