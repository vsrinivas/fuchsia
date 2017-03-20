#include <magenta/device/intel-hda.h>
#include <mxio/io.h>

#include "intel_hda_device.h"

namespace audio {
namespace intel_hda {

mx_status_t IntelHDADevice::Probe() {
    mx_status_t res = MagentaDevice::Connect();
    if (res != NO_ERROR)
        return res;

    ihda_get_ids_req_t req;
    ihda_get_ids_resp_t resp;

    InitRequest(&req, IHDA_CMD_GET_IDS);
    res = CallDevice(req, &resp);
    if (res != NO_ERROR)
        return res;

    vid_       = resp.vid;
    did_       = resp.did;
    ihda_vmaj_ = resp.ihda_vmaj;
    ihda_vmin_ = resp.ihda_vmin;
    rev_id_    = resp.rev_id;
    step_id_   = resp.step_id;

    return NO_ERROR;
}

}  // namespace audio
}  // namespace intel_hda
