/*
 * Copyright (C) 2020 The Fuchsia Authors.
 * Copyright (C) 2008 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef SRC_LIB_USB_BULK_CPP_USB_H_
#define SRC_LIB_USB_BULK_CPP_USB_H_

#include <stdint.h>
#include <sys/types.h>

extern "C" {

struct usb_ifc_info {
  /* from device descriptor */
  unsigned short dev_vendor;
  unsigned short dev_product;

  unsigned char dev_class;
  unsigned char dev_subclass;
  unsigned char dev_protocol;

  unsigned char ifc_class;
  unsigned char ifc_subclass;
  unsigned char ifc_protocol;

  unsigned char has_bulk_in;
  unsigned char has_bulk_out;

  unsigned char writable;

  uint8_t serial_number[256];
  uint8_t device_path[256];
};

typedef bool (*ifc_match_func)(usb_ifc_info *ifc, void *data);

class UsbInterface;

// 0 is non blocking
UsbInterface *interface_open(ifc_match_func callback, void *callback_data, uint32_t timeout_ms = 0);
ssize_t interface_read(UsbInterface *interface, void *data, ssize_t len);
ssize_t interface_write(UsbInterface *interface, const void *data, ssize_t len);
void interface_close(UsbInterface *interface);
void interface_wait_for_disconnect(UsbInterface *interface);

}

#endif  // SRC_LIB_USB_BULK_CPP_USB_H_
