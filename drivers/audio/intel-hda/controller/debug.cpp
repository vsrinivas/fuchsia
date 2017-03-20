// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/assert.h>
#include <mxtl/auto_lock.h>
#include <string.h>

#include "drivers/audio/intel-hda/utils/intel-hda-registers.h"

#include "codec-cmd-job.h"
#include "debug-logging.h"
#include "intel-hda-controller.h"
#include "thread-annotations.h"

namespace audio {
namespace intel_hda {

namespace {
mxtl::Mutex snapshot_regs_buffer_lock;
ihda_controller_snapshot_regs_resp_t snapshot_regs_buffer TA_GUARDED(snapshot_regs_buffer_lock);
}  // anon namespace

mx_status_t IntelHDAController::SnapshotRegs(const DispatcherChannel& channel,
                                             const ihda_controller_snapshot_regs_req_t& req) {
    // TODO(johngro) : What an enormous PITA.  Every register needs to be
    // accessed with the proper sized transaction on the PCI bus, so we cannot
    // just use memcpy to do this.  Life will be better when we have VMOs in
    // place.  Then, we can implement the IOCTL by simply cloning the reigster
    // VMO (reducing its rights to read-only in the process) and sending it back
    // to the calling process.  The diagnostic util can then put their own
    // cycles on the PCI bus.
    mxtl::AutoLock lock(&snapshot_regs_buffer_lock);

    auto  regs_ptr = reinterpret_cast<hda_registers_t*>(snapshot_regs_buffer.snapshot);
    auto& regs     = *regs_ptr;

    static_assert(sizeof(snapshot_regs_buffer.snapshot) == sizeof(hda_registers_t),
                  "Register snapshot buffer size does not match  register file size!");

    snapshot_regs_buffer.hdr = req.hdr;
    memset(&regs, 0, sizeof(regs));

    regs.gcap       = REG_RD(&regs_->gcap);
    regs.vmin       = REG_RD(&regs_->vmin);
    regs.vmaj       = REG_RD(&regs_->vmaj);
    regs.outpay     = REG_RD(&regs_->outpay);
    regs.inpay      = REG_RD(&regs_->inpay);
    regs.gctl       = REG_RD(&regs_->gctl);
    regs.wakeen     = REG_RD(&regs_->wakeen);
    regs.statests   = REG_RD(&regs_->statests);
    regs.gsts       = REG_RD(&regs_->gsts);
    regs.outstrmpay = REG_RD(&regs_->outstrmpay);
    regs.instrmpay  = REG_RD(&regs_->instrmpay);
    regs.intctl     = REG_RD(&regs_->intctl);
    regs.intsts     = REG_RD(&regs_->intsts);
    regs.walclk     = REG_RD(&regs_->walclk);
    regs.ssync      = REG_RD(&regs_->ssync);
    regs.corblbase  = REG_RD(&regs_->corblbase);
    regs.corbubase  = REG_RD(&regs_->corbubase);
    regs.corbwp     = REG_RD(&regs_->corbwp);
    regs.corbrp     = REG_RD(&regs_->corbrp);
    regs.corbctl    = REG_RD(&regs_->corbctl);
    regs.corbsts    = REG_RD(&regs_->corbsts);
    regs.corbsize   = REG_RD(&regs_->corbsize);
    regs.rirblbase  = REG_RD(&regs_->rirblbase);
    regs.rirbubase  = REG_RD(&regs_->rirbubase);
    regs.rirbwp     = REG_RD(&regs_->rirbwp);
    regs.rintcnt    = REG_RD(&regs_->rintcnt);
    regs.rirbctl    = REG_RD(&regs_->rirbctl);
    regs.rirbsts    = REG_RD(&regs_->rirbsts);
    regs.rirbsize   = REG_RD(&regs_->rirbsize);
    regs.icoi       = REG_RD(&regs_->icoi);
    regs.icii       = REG_RD(&regs_->icii);
    regs.icis       = REG_RD(&regs_->icis);
    regs.dpiblbase  = REG_RD(&regs_->dpiblbase);
    regs.dpibubase  = REG_RD(&regs_->dpibubase);

    uint16_t gcap = REG_RD(&regs_->gcap);
    unsigned int stream_cnt = HDA_REG_GCAP_ISS(gcap)
                            + HDA_REG_GCAP_OSS(gcap)
                            + HDA_REG_GCAP_BSS(gcap);

    for (unsigned int i = 0; i < stream_cnt; ++i) {
        auto& sin  = regs_->stream_desc[i];
        auto& sout = regs.stream_desc[i];

        sout.ctl_sts.w = REG_RD(&sin.ctl_sts.w);
        sout.lpib      = REG_RD(&sin.lpib);
        sout.cbl       = REG_RD(&sin.cbl);
        sout.lvi       = REG_RD(&sin.lvi);
        sout.fifod     = REG_RD(&sin.fifod);
        sout.fmt       = REG_RD(&sin.fmt);
        sout.bdpl      = REG_RD(&sin.bdpl);
        sout.bdpu      = REG_RD(&sin.bdpu);
    }

    return channel.Write(&snapshot_regs_buffer, sizeof(snapshot_regs_buffer));
}

}  // namespace intel_hda
}  // namespace audio
