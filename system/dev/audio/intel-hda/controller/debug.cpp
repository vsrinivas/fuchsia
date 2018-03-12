// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>
#include <fbl/auto_lock.h>
#include <string.h>

#include <intel-hda/utils/intel-hda-registers.h>

#include "codec-cmd-job.h"
#include "debug-logging.h"
#include "intel-hda-controller.h"
#include "thread-annotations.h"

namespace audio {
namespace intel_hda {

namespace {
fbl::Mutex snapshot_regs_buffer_lock;
ihda_controller_snapshot_regs_resp_t snapshot_regs_buffer TA_GUARDED(snapshot_regs_buffer_lock);
}  // anon namespace

zx_status_t IntelHDAController::SnapshotRegs(dispatcher::Channel* channel,
                                             const ihda_controller_snapshot_regs_req_t& req) {
    ZX_DEBUG_ASSERT(channel != nullptr);

    // TODO(johngro) : What an enormous PITA.  Every register needs to be
    // accessed with the proper sized transaction on the PCI bus, so we cannot
    // just use memcpy to do this.  Life will be better when we have VMOs in
    // place.  Then, we can implement the IOCTL by simply cloning the reigster
    // VMO (reducing its rights to read-only in the process) and sending it back
    // to the calling process.  The diagnostic util can then put their own
    // cycles on the PCI bus.
    fbl::AutoLock lock(&snapshot_regs_buffer_lock);

    auto  regs_ptr = reinterpret_cast<hda_registers_t*>(snapshot_regs_buffer.snapshot);
    auto& out_regs = *regs_ptr;

    static_assert(sizeof(snapshot_regs_buffer.snapshot) == sizeof(hda_registers_t),
                  "Register snapshot buffer size does not match  register file size!");

    snapshot_regs_buffer.hdr = req.hdr;
    memset(&out_regs, 0, sizeof(out_regs));

    out_regs.gcap       = REG_RD(&regs()->gcap);
    out_regs.vmin       = REG_RD(&regs()->vmin);
    out_regs.vmaj       = REG_RD(&regs()->vmaj);
    out_regs.outpay     = REG_RD(&regs()->outpay);
    out_regs.inpay      = REG_RD(&regs()->inpay);
    out_regs.gctl       = REG_RD(&regs()->gctl);
    out_regs.wakeen     = REG_RD(&regs()->wakeen);
    out_regs.statests   = REG_RD(&regs()->statests);
    out_regs.gsts       = REG_RD(&regs()->gsts);
    out_regs.outstrmpay = REG_RD(&regs()->outstrmpay);
    out_regs.instrmpay  = REG_RD(&regs()->instrmpay);
    out_regs.intctl     = REG_RD(&regs()->intctl);
    out_regs.intsts     = REG_RD(&regs()->intsts);
    out_regs.walclk     = REG_RD(&regs()->walclk);
    out_regs.ssync      = REG_RD(&regs()->ssync);
    out_regs.corblbase  = REG_RD(&regs()->corblbase);
    out_regs.corbubase  = REG_RD(&regs()->corbubase);
    out_regs.corbwp     = REG_RD(&regs()->corbwp);
    out_regs.corbrp     = REG_RD(&regs()->corbrp);
    out_regs.corbctl    = REG_RD(&regs()->corbctl);
    out_regs.corbsts    = REG_RD(&regs()->corbsts);
    out_regs.corbsize   = REG_RD(&regs()->corbsize);
    out_regs.rirblbase  = REG_RD(&regs()->rirblbase);
    out_regs.rirbubase  = REG_RD(&regs()->rirbubase);
    out_regs.rirbwp     = REG_RD(&regs()->rirbwp);
    out_regs.rintcnt    = REG_RD(&regs()->rintcnt);
    out_regs.rirbctl    = REG_RD(&regs()->rirbctl);
    out_regs.rirbsts    = REG_RD(&regs()->rirbsts);
    out_regs.rirbsize   = REG_RD(&regs()->rirbsize);
    out_regs.icoi       = REG_RD(&regs()->icoi);
    out_regs.icii       = REG_RD(&regs()->icii);
    out_regs.icis       = REG_RD(&regs()->icis);
    out_regs.dpiblbase  = REG_RD(&regs()->dpiblbase);
    out_regs.dpibubase  = REG_RD(&regs()->dpibubase);

    uint16_t gcap = REG_RD(&regs()->gcap);
    unsigned int stream_cnt = HDA_REG_GCAP_ISS(gcap)
                            + HDA_REG_GCAP_OSS(gcap)
                            + HDA_REG_GCAP_BSS(gcap);

    for (unsigned int i = 0; i < stream_cnt; ++i) {
        auto& sin  = regs()->stream_desc[i];
        auto& sout = out_regs.stream_desc[i];

        sout.ctl_sts.w = REG_RD(&sin.ctl_sts.w);
        sout.lpib      = REG_RD(&sin.lpib);
        sout.cbl       = REG_RD(&sin.cbl);
        sout.lvi       = REG_RD(&sin.lvi);
        sout.fifod     = REG_RD(&sin.fifod);
        sout.fmt       = REG_RD(&sin.fmt);
        sout.bdpl      = REG_RD(&sin.bdpl);
        sout.bdpu      = REG_RD(&sin.bdpu);
    }

    return channel->Write(&snapshot_regs_buffer, sizeof(snapshot_regs_buffer));
}

}  // namespace intel_hda
}  // namespace audio
