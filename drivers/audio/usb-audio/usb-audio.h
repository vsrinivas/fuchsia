// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <driver/usb.h>
#include <magenta/compiler.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-audio.h>

__BEGIN_CDECLS

mx_status_t usb_audio_source_create(mx_device_t* device, usb_protocol_t* usb, int index,
                                    usb_interface_descriptor_t* intf,
                                    usb_endpoint_descriptor_t* ep,
                                    usb_audio_ac_format_type_i_desc* format_desc);

mx_status_t usb_midi_sink_create(mx_device_t* device, usb_protocol_t* usb, int index,
                                 usb_interface_descriptor_t* intf,
                                 usb_endpoint_descriptor_t* ep);

mx_status_t usb_midi_source_create(mx_device_t* device, usb_protocol_t* usb, int index,
                                   usb_interface_descriptor_t* intf,
                                   usb_endpoint_descriptor_t* ep);


uint32_t* usb_audio_parse_sample_rates(usb_audio_ac_format_type_i_desc* format_desc,
                                       int* out_count);

mx_status_t usb_audio_set_sample_rate(usb_protocol_t* usb, uint8_t ep_addr,
                                      uint32_t sample_rate);

// volume is in 0 - 100 range
mx_status_t usb_audio_set_volume(usb_protocol_t* usb, uint8_t interface_number, int fu_id,
                                 int volume);

__END_CDECLS
