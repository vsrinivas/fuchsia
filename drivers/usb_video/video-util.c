// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <driver/usb.h>
#include <string.h>

#include "video-util.h"

static void print_controls(usb_video_vc_probe_and_commit_controls* proposal) {
  zxlogf(TRACE, "bmHint 0x%x\n", proposal->bmHint);
  zxlogf(TRACE, "bFormatIndex: %u\n", proposal->bFormatIndex);
  zxlogf(TRACE, "bFrameIndex: %u\n", proposal->bFrameIndex);
  zxlogf(TRACE, "dwFrameInterval: %u\n", proposal->dwFrameInterval);
  zxlogf(TRACE, "dwMaxVideoFrameSize: %u\n", proposal->dwMaxVideoFrameSize);
  zxlogf(TRACE, "dwMaxPayloadTransferSize: %u\n",
         proposal->dwMaxPayloadTransferSize);
}

zx_status_t usb_video_negotiate_probe(
    usb_protocol_t* usb, uint8_t vs_interface_num,
    usb_video_vc_probe_and_commit_controls* proposal,
    usb_video_vc_probe_and_commit_controls* out_result) {
  zx_status_t status;
  size_t out_length;

  zxlogf(TRACE, "usb_video_negotiate_probe: PROBE_CONTROL SET_CUR\n");
  print_controls(proposal);
  // The wValue field (the fourth parameter) specifies the Control Selector
  // (in this case USB_VIDEO_VS_PROBE_CONTROL) in the high byte,
  // and the low byte must be set to zero.
  // See UVC 1.5 Spec. 4.2.1 Interface Control Requests.
  status = usb_control(usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                       USB_VIDEO_SET_CUR, USB_VIDEO_VS_PROBE_CONTROL << 8,
                       vs_interface_num, proposal, sizeof(*proposal),
                       ZX_TIME_INFINITE, NULL);
  if (status != ZX_OK)
    goto out;

  // The length of returned result varies, so zero this out before hand.
  memset(out_result, 0, sizeof(usb_video_vc_probe_and_commit_controls));

  zxlogf(TRACE, "usb_video_negotiate_probe: PROBE_CONTROL GET_CUR\n");
  status = usb_control(usb, USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                       USB_VIDEO_GET_CUR, USB_VIDEO_VS_PROBE_CONTROL << 8,
                       vs_interface_num, out_result, sizeof(*out_result),
                       ZX_TIME_INFINITE, &out_length);
  if (status != ZX_OK) {
    goto out;
  }
  // Fields after dwMaxPayloadTransferSize are optional, only 26 bytes are
  // guaranteed.
  if (out_length < 26) {
    zxlogf(ERROR, "usb_video_negotiate_probe: got length %lu, want >= 26\n",
           out_length);
    goto out;
  }
  print_controls(out_result);

out:
  if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
    // clear the stall/error
    usb_reset_endpoint(usb, 0);
  }
  return status;
}

zx_status_t usb_video_negotiate_commit(
    usb_protocol_t* usb, uint8_t vs_interface_num,
    usb_video_vc_probe_and_commit_controls* ctrls) {
  zxlogf(TRACE, "usb_video_negotiate_commit: COMMIT_CONTROL SET_CUR\n");
  zx_status_t status = usb_control(
      usb, USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
      USB_VIDEO_SET_CUR, USB_VIDEO_VS_COMMIT_CONTROL << 8, vs_interface_num,
      ctrls, sizeof(*ctrls), ZX_TIME_INFINITE, NULL);
  if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
    // clear the stall/error
    usb_reset_endpoint(usb, 0);
  }
  return status;
}
