// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <driver/usb.h>
#include <zircon/hw/usb-video.h>

__BEGIN_CDECLS

// The probe stage of the stream negotiation process.
//   usb: the device protocol.
//   vs_interface_num: the number of the interface to direct the request to.
//   proposal: the desired streaming parameters, such as which format to use.
//   out_result: the new negotiated streaming parameters returned from the
//   device.
zx_status_t usb_video_negotiate_probe(
    usb_protocol_t* usb, uint8_t vs_interface_num,
    usb_video_vc_probe_and_commit_controls* proposal,
    usb_video_vc_probe_and_commit_controls* out_result);

// The commit state of the stream negotiation process.
//   usb: the device protocol.
//   vs_interface_num: the number of the interface to direct the request to.
//   ctrls: used to configure the hardware, should be the result of
//          usb_video_negotiate_probe.
zx_status_t usb_video_negotiate_commit(
    usb_protocol_t* usb, uint8_t vs_interface_num,
    usb_video_vc_probe_and_commit_controls* ctrls);

__END_CDECLS
