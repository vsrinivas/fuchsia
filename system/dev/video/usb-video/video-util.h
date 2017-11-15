// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <driver/usb.h>
#include <zircon/hw/usb-video.h>

__BEGIN_CDECLS

zx_status_t usb_video_negotiate_stream(usb_protocol_t* usb, uint8_t vs_interface_num,
                                       usb_video_vc_probe_and_commit_controls* proposal,
                                       usb_video_vc_probe_and_commit_controls* out_result);

__END_CDECLS
