// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <driver/usb.h>
#include <zircon/compiler.h>
#include <zircon/hw/usb.h>
#include <zircon/hw/usb-audio.h>

__BEGIN_CDECLS

zx_status_t usb_audio_source_create(zx_device_t* device, usb_protocol_t* usb, int index,
                                    usb_interface_descriptor_t* intf,
                                    usb_endpoint_descriptor_t* ep,
                                    usb_audio_ac_format_type_i_desc* format_desc);

zx_status_t usb_midi_sink_create(zx_device_t* device, usb_protocol_t* usb, int index,
                                 usb_interface_descriptor_t* intf,
                                 usb_endpoint_descriptor_t* ep);

zx_status_t usb_midi_source_create(zx_device_t* device, usb_protocol_t* usb, int index,
                                   usb_interface_descriptor_t* intf,
                                   usb_endpoint_descriptor_t* ep);


uint32_t* usb_audio_parse_sample_rates(usb_audio_ac_format_type_i_desc* format_desc,
                                       int* out_count);

zx_status_t usb_audio_set_sample_rate(usb_protocol_t* usb, uint8_t ep_addr,
                                      uint32_t sample_rate);

void usb_audio_dump_feature_unit_caps(usb_protocol_t* usb,
                                      uint8_t interface_number,
                                      const usb_audio_ac_feature_unit_desc* fu_desc);

// volume is in 0 - 100 range
zx_status_t usb_audio_set_volume(usb_protocol_t* usb,
                                 uint8_t interface_number,
                                 const usb_audio_ac_feature_unit_desc* fu_desc,
                                 int volume);

__END_CDECLS
