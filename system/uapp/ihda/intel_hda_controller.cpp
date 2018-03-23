// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/intel-hda.h>
#include <fdio/io.h>

#include <intel-hda/utils/intel-hda-registers.h>

#include "intel_hda_controller.h"

namespace audio {
namespace intel_hda {

IntelHDAController::ControllerTree IntelHDAController::controllers_;

static int ihda_dump_sdctl(const char* name, const void* base, size_t offset, bool crlf = true) {
    uint32_t val = *reinterpret_cast<int32_t*>(reinterpret_cast<intptr_t>(base) + offset);
    val &= 0xFFFFFF;
    return printf("[%02zx] %10s : %06x   (%u)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static int ihda_dump32(const char* name, const void* base, size_t offset, bool crlf = true) {
    uint32_t val = *reinterpret_cast<uint32_t*>(reinterpret_cast<intptr_t>(base) + offset);
    return printf("[%02zx] %10s : %08x (%u)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static int ihda_dump16(const char* name, const void* base, size_t offset, bool crlf = true) {
    uint16_t val = *reinterpret_cast<uint16_t*>(reinterpret_cast<intptr_t>(base) + offset);
    return printf("[%02zx] %10s : %04hx     (%hu)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static int ihda_dump8(const char* name, const void* base, size_t offset, bool crlf = true) {
    uint8_t val = *reinterpret_cast<int8_t*>(reinterpret_cast<intptr_t>(base) + offset);
    return printf("[%02zx] %10s : %02x       (%u)%s",
                  offset, name, val, val, crlf ? "\n" : "");
}

static void pad(int done, int width) {
    if (done < 0) return;
    while (done < width) {
        printf(" ");
        done++;
    }
}

static void ihda_dump_stream_regs(const char* name,
                                  size_t count,
                                  const hda_stream_desc_regs_t* regs) {
    static const struct {
        const char* name;
        int (*dump_fn)(const char*, const void*, size_t, bool);
        size_t offset;
    } STREAM_REGS[] = {
        { "CTL",   ihda_dump_sdctl, offsetof(hda_stream_desc_regs_t, ctl_sts.w) },
        { "STS",   ihda_dump8,      offsetof(hda_stream_desc_regs_t, ctl_sts.b.sts) },
        { "LPIB",  ihda_dump32,     offsetof(hda_stream_desc_regs_t, lpib) },
        { "CBL",   ihda_dump32,     offsetof(hda_stream_desc_regs_t, cbl) },
        { "LVI",   ihda_dump16,     offsetof(hda_stream_desc_regs_t, lvi) },
        { "FIFOD", ihda_dump16,     offsetof(hda_stream_desc_regs_t, fifod) },
        { "FMT",   ihda_dump16,     offsetof(hda_stream_desc_regs_t, fmt) },
        { "BDPL",  ihda_dump32,     offsetof(hda_stream_desc_regs_t, bdpl) },
        { "BDPU",  ihda_dump32,     offsetof(hda_stream_desc_regs_t, bdpu) },
    };
    static const size_t COLUMNS = 4;
    static const int    COLUMN_WIDTH = 40;
    int done;

    for (size_t i = 0; i < count; i += COLUMNS) {
        size_t todo = count - i;
        if (todo > COLUMNS)
            todo = COLUMNS;

        printf("\n");
        for (size_t j = 0; j < todo; ++j) {
            done = printf("%s %zu/%zu", name, i + j + 1, count);
            if ((j + 1) < todo)
                pad(done, COLUMN_WIDTH);
        }
        printf("\n");

        for (size_t reg = 0; reg < countof(STREAM_REGS); ++reg) {
            for (size_t j = 0; j < todo; ++j) {
                const hda_stream_desc_regs_t* r = regs + i + j;
                done = STREAM_REGS[reg].dump_fn(STREAM_REGS[reg].name,
                                                r,
                                                STREAM_REGS[reg].offset,
                                                false);
                if ((j + 1) < todo)
                    pad(done, COLUMN_WIDTH);
            }
            printf("\n");
        }
    }
}

zx_status_t IntelHDAController::Enumerate() {
    static const char* const DEV_PATH = "/dev/class/intel-hda";

    zx_status_t res = ZirconDevice::Enumerate(nullptr, DEV_PATH,
    [](void*, uint32_t id, const char* const dev_name) -> zx_status_t {
        fbl::AllocChecker ac;
        fbl::unique_ptr<IntelHDAController> dev(new (&ac) IntelHDAController(id, dev_name));
        if (!ac.check()) {
            return ZX_ERR_NO_MEMORY;
        }

        if (!controllers_.insert_or_find(fbl::move(dev)))
            return ZX_ERR_INTERNAL;

        return ZX_OK;
    });

    if (res != ZX_OK)
        return res;

    return ZX_OK;
}

zx_status_t IntelHDAController::DumpRegs(int argc, const char** argv) {
    zx_status_t res = Connect();

    if (res != ZX_OK)
        return res;

    ihda_controller_snapshot_regs_req_t req;
    ihda_controller_snapshot_regs_resp_t resp;

    InitRequest(&req, IHDA_CONTROLLER_CMD_SNAPSHOT_REGS);
    res = CallDevice(req, &resp);
    if (res != ZX_OK)
        return res;

    const auto  regs_ptr = reinterpret_cast<hda_registers_t*>(resp.snapshot);
    const auto& regs     = *regs_ptr;

    printf("Registers for Intel HDA Device #%u\n", id_);

    ihda_dump16("GCAP",       &regs, offsetof(hda_registers_t, gcap));
    ihda_dump8 ("VMIN",       &regs, offsetof(hda_registers_t, vmin));
    ihda_dump8 ("VMAJ",       &regs, offsetof(hda_registers_t, vmaj));
    ihda_dump16("OUTPAY",     &regs, offsetof(hda_registers_t, outpay));
    ihda_dump16("INPAY",      &regs, offsetof(hda_registers_t, inpay));
    ihda_dump32("GCTL",       &regs, offsetof(hda_registers_t, gctl));
    ihda_dump16("WAKEEN",     &regs, offsetof(hda_registers_t, wakeen));
    ihda_dump16("STATESTS",   &regs, offsetof(hda_registers_t, statests));
    ihda_dump16("GSTS",       &regs, offsetof(hda_registers_t, gsts));
    ihda_dump16("OUTSTRMPAY", &regs, offsetof(hda_registers_t, outstrmpay));
    ihda_dump16("INSTRMPAY",  &regs, offsetof(hda_registers_t, instrmpay));
    ihda_dump32("INTCTL",     &regs, offsetof(hda_registers_t, intctl));
    ihda_dump32("INTSTS",     &regs, offsetof(hda_registers_t, intsts));
    ihda_dump32("WALCLK",     &regs, offsetof(hda_registers_t, walclk));
    ihda_dump32("SSYNC",      &regs, offsetof(hda_registers_t, ssync));
    ihda_dump32("CORBLBASE",  &regs, offsetof(hda_registers_t, corblbase));
    ihda_dump32("CORBUBASE",  &regs, offsetof(hda_registers_t, corbubase));
    ihda_dump16("CORBWP",     &regs, offsetof(hda_registers_t, corbwp));
    ihda_dump16("CORBRP",     &regs, offsetof(hda_registers_t, corbrp));
    ihda_dump8 ("CORBCTL",    &regs, offsetof(hda_registers_t, corbctl));
    ihda_dump8 ("CORBSTS",    &regs, offsetof(hda_registers_t, corbsts));
    ihda_dump8 ("CORBSIZE",   &regs, offsetof(hda_registers_t, corbsize));
    ihda_dump32("RIRBLBASE",  &regs, offsetof(hda_registers_t, rirblbase));
    ihda_dump32("RIRBUBASE",  &regs, offsetof(hda_registers_t, rirbubase));
    ihda_dump16("RIRBWP",     &regs, offsetof(hda_registers_t, rirbwp));
    ihda_dump16("RINTCNT",    &regs, offsetof(hda_registers_t, rintcnt));
    ihda_dump8 ("RIRBCTL",    &regs, offsetof(hda_registers_t, rirbctl));
    ihda_dump8 ("RIRBSTS",    &regs, offsetof(hda_registers_t, rirbsts));
    ihda_dump8 ("RIRBSIZE",   &regs, offsetof(hda_registers_t, rirbsize));
    ihda_dump32("ICOI",       &regs, offsetof(hda_registers_t, icoi));
    ihda_dump32("ICII",       &regs, offsetof(hda_registers_t, icii));
    ihda_dump16("ICIS",       &regs, offsetof(hda_registers_t, icis));
    ihda_dump32("DPIBLBASE",  &regs, offsetof(hda_registers_t, dpiblbase));
    ihda_dump32("DPIBUBASE",  &regs, offsetof(hda_registers_t, dpibubase));

    uint16_t gcap = REG_RD(&regs.gcap);
    unsigned int input_stream_cnt  = HDA_REG_GCAP_ISS(gcap);
    unsigned int output_stream_cnt = HDA_REG_GCAP_OSS(gcap);
    unsigned int bidir_stream_cnt  = HDA_REG_GCAP_BSS(gcap);
    const hda_stream_desc_regs_t* sregs = regs.stream_desc;

    ihda_dump_stream_regs("Input Stream",  input_stream_cnt,  sregs); sregs += input_stream_cnt;
    ihda_dump_stream_regs("Output Stream", output_stream_cnt, sregs); sregs += output_stream_cnt;
    ihda_dump_stream_regs("Bi-dir Stream", bidir_stream_cnt,  sregs);

    return ZX_OK;
}

}  // namespace audio
}  // namespace intel_hda
