// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <driver-info/driver-info.h>

#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <magenta/driver/binding.h>
#include <magenta/types.h>

typedef Elf64_Ehdr elfhdr;
typedef Elf64_Phdr elfphdr;
typedef Elf64_Nhdr notehdr;

static mx_status_t find_note(const char* name, size_t nlen, uint32_t type,
                             void* data, size_t size,
                             mx_status_t (*func)(void* note, size_t sz,
                                                 void* cookie),
                             void* cookie) {
    while (size >= sizeof(notehdr)) {
        const notehdr* hdr = data;
        uint32_t nsz = (hdr->n_namesz + 3) & (~3);
        if (nsz > (size - sizeof(notehdr))) {
            return MX_ERR_INTERNAL;
        }
        size_t hsz = sizeof(notehdr) + nsz;
        data += hsz;
        size -= hsz;

        uint32_t dsz = (hdr->n_descsz + 3) & (~3);
        if (dsz > size) {
            return MX_ERR_INTERNAL;
        }

        if ((hdr->n_type == type) &&
            (hdr->n_namesz == nlen) &&
            (memcmp(name, hdr + 1, nlen) == 0)) {
            return func(data - hsz, hdr->n_descsz + hsz, cookie);
        }

        data += dsz;
        size -= dsz;
    }
    return MX_ERR_NOT_FOUND;
}

static mx_status_t for_each_note(int fd, const char* name, uint32_t type,
                                 void* data, size_t dsize,
                                 mx_status_t (*func)(void* note,
                                                     size_t sz, void* cookie),
                                 void* cookie) {
    size_t nlen = strlen(name) + 1;
    elfphdr ph[64];
    elfhdr eh;
    if (pread(fd, &eh, sizeof(eh), 0) != sizeof(eh)) {
        printf("for_each_note: pread(eh) failed\n");
        return MX_ERR_IO;
    }
    if (memcmp(&eh, ELFMAG, 4) ||
        (eh.e_ehsize != sizeof(elfhdr)) ||
        (eh.e_phentsize != sizeof(elfphdr))) {
        printf("for_each_note: bad elf magic\n");
        return MX_ERR_INTERNAL;
    }
    size_t sz = sizeof(elfphdr) * eh.e_phnum;
    if (sz > sizeof(ph)) {
        printf("for_each_note: too many phdrs\n");
        return MX_ERR_INTERNAL;
    }
    if ((pread(fd, ph, sz, eh.e_phoff) != (ssize_t)sz)) {
        printf("for_each_note: pread(sz,eh.e_phoff) failed\n");
        return MX_ERR_IO;
    }
    for (int i = 0; i < eh.e_phnum; i++) {
        if ((ph[i].p_type != PT_NOTE) ||
            (ph[i].p_filesz > dsize)) {
            continue;
        }
        if ((pread(fd, data, ph[i].p_filesz, ph[i].p_offset) != (ssize_t)ph[i].p_filesz)) {
            printf("for_each_note: pread(ph[i]) failed\n");
            return MX_ERR_IO;
        }
        int r = find_note(name, nlen, type, data, ph[i].p_filesz, func, cookie);
        if (r == MX_OK) {
            return r;
        }
    }
    return MX_ERR_NOT_FOUND;
}

typedef struct {
    void* cookie;
    void (*func)(magenta_driver_note_payload_t* note,
                 const mx_bind_inst_t* binding, void* cookie);
} context;

static mx_status_t callback(void* note, size_t sz, void* _ctx) {
    context* ctx = _ctx;
    magenta_driver_note_t* dn = note;
    if (sz < sizeof(magenta_driver_note_t)) {
        return MX_ERR_INTERNAL;
    }
    size_t max = (sz - sizeof(magenta_driver_note_t)) / sizeof(mx_bind_inst_t);
    if (dn->payload.bindcount > max) {
        return MX_ERR_INTERNAL;
    }
    const mx_bind_inst_t* binding = (const void*)(&dn->payload + 1);
    ctx->func(&dn->payload, binding, ctx->cookie);
    return MX_OK;
}

mx_status_t di_read_driver_info(int fd, void *cookie,
                                void (*func)(
                                    magenta_driver_note_payload_t* note,
                                    const mx_bind_inst_t* binding,
                                    void *cookie)) {
    context ctx = {
        .cookie = cookie,
        .func = func,
    };
    uint8_t data[4096];
    return for_each_note(fd, MAGENTA_NOTE_NAME, MAGENTA_NOTE_DRIVER,
                         data, sizeof(data), callback, &ctx);
}

const char* di_bind_param_name(uint32_t param_num) {
    switch (param_num) {
    case BIND_FLAGS:                  return "Flags";
    case BIND_PROTOCOL:               return "Protocol";
    case BIND_AUTOBIND:               return "Autobind";
    case BIND_PCI_VID:                return "PCI.VID";
    case BIND_PCI_DID:                return "PCI.DID";
    case BIND_PCI_CLASS:              return "PCI.Class";
    case BIND_PCI_SUBCLASS:           return "PCI.Subclass";
    case BIND_PCI_INTERFACE:          return "PCI.Interface";
    case BIND_PCI_REVISION:           return "PCI.Revision";
    case BIND_PCI_BDF_ADDR:           return "PCI.BDFAddr";
    case BIND_USB_VID:                return "USB.VID";
    case BIND_USB_PID:                return "USB.PID";
    case BIND_USB_CLASS:              return "USB.Class";
    case BIND_USB_SUBCLASS:           return "USB.Subclass";
    case BIND_USB_PROTOCOL:           return "USB.Protocol";
    case BIND_PLATFORM_DEV_VID:       return "PlatDev.VID";
    case BIND_PLATFORM_DEV_PID:       return "PlatDev.PID";
    case BIND_PLATFORM_DEV_DID:       return "PlatDev.DID";
    case BIND_ACPI_HID_0_3:           return "ACPI.HID[0-3]";
    case BIND_ACPI_HID_4_7:           return "ACPI.HID[4-7]";
    case BIND_IHDA_CODEC_VID:         return "IHDA.Codec.VID";
    case BIND_IHDA_CODEC_DID:         return "IHDA.Codec.DID";
    case BIND_IHDA_CODEC_MAJOR_REV:   return "IHDACodec.MajorRev";
    case BIND_IHDA_CODEC_MINOR_REV:   return "IHDACodec.MinorRev";
    case BIND_IHDA_CODEC_VENDOR_REV:  return "IHDACodec.VendorRev";
    case BIND_IHDA_CODEC_VENDOR_STEP: return "IHDACodec.VendorStep";
    default: return NULL;
    }
}

void di_dump_bind_inst(const mx_bind_inst_t* b, char* buf, size_t buf_len) {
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
        snprintf(buf + off, buf_len - off, "L.%u:", pa);
        return;
    default:
        snprintf(buf + off, buf_len - off,
                "Unknown Op 0x%1x [0x%08x, 0x%08x]", op, b->op, b->arg);
        return;
    }

    off += snprintf(buf + off, buf_len - off, "if (");
    if (cc == COND_AL) {
        off += snprintf(buf + off, buf_len - off, "true");
    } else {
        const char* pb_name = di_bind_param_name(pb);
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
