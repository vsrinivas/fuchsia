// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>
#include <zircon/hw/usb-audio.h>
#include <zircon/listnode.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

#include "usb-audio.h"

//#define TRACE 1
#if TRACE
#define xprintf(fmt...) printf(fmt)
#else
#define xprintf(fmt...) \
    do {                \
    } while (0)
#endif

extern zx_status_t usb_audio_sink_create(zx_device_t* device, usb_protocol_t* usb, int index,
                                         usb_interface_descriptor_t* intf,
                                         usb_endpoint_descriptor_t* ep,
                                         usb_audio_ac_format_type_i_desc* format_desc);


// for list of feature unit descriptors
typedef struct {
    list_node_t node;
    usb_audio_ac_feature_unit_desc* desc;
    uint8_t intf_num;
} feature_unit_node_t;

static bool want_interface(usb_interface_descriptor_t* intf, void* arg) {
    return (intf->bInterfaceClass == USB_CLASS_AUDIO &&
            intf->bInterfaceSubClass != USB_SUBCLASS_AUDIO_CONTROL);
}

static zx_status_t usb_audio_bind(void* ctx, zx_device_t* device) {
    usb_protocol_t usb;
    zx_status_t status = device_get_protocol(device, ZX_PROTOCOL_USB, &usb);
    if (status != ZX_OK) {
        return status;
    }
    status = usb_claim_additional_interfaces(&usb, want_interface, NULL);
    if (status != ZX_OK) {
        return status;
    }

    // find our endpoints
    usb_desc_iter_t iter;
    status = usb_desc_iter_init(&usb, &iter);
    if (status < 0) return status;
    int audio_sink_index = 0;
    int audio_source_index = 0;
    int midi_sink_index = 0;
    int midi_source_index = 0;

    usb_descriptor_header_t* header;
    // most recent USB interface descriptor
    usb_interface_descriptor_t* intf = NULL;
    // format descriptor for current audio streaming interface
    usb_audio_ac_format_type_i_desc* format_desc = NULL;
    // feature unit desciptor for current audio streaming interface

    list_node_t fu_descs = LIST_INITIAL_VALUE(fu_descs);

    while ((header = usb_desc_iter_next(&iter)) != NULL) {
        switch (header->bDescriptorType) {
        case USB_DT_INTERFACE: {
                intf = (usb_interface_descriptor_t *)header;
                if (intf->bInterfaceClass == USB_CLASS_AUDIO) {
                    if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_CONTROL) {
                        xprintf("interface USB_SUBCLASS_AUDIO_CONTROL\n");
                        break;
                    } else if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_STREAMING) {
                        xprintf("interface USB_SUBCLASS_AUDIO_STREAMING bAlternateSetting: %d\n",
                                intf->bAlternateSetting);
                        // reset format and feature unit descriptors
                        format_desc = NULL;
                        break;
                    } else if (intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING) {
                        xprintf("interface USB_SUBCLASS_MIDI_STREAMING bAlternateSetting: %d\n",
                                intf->bAlternateSetting);
                        break;
                    }
                }
                xprintf("USB_DT_INTERFACE %d %d %d\n", intf->bInterfaceClass, intf->bInterfaceSubClass,
                        intf->bInterfaceProtocol);
                break;
        }
        case USB_DT_ENDPOINT: {
                usb_endpoint_descriptor_t* endp = (usb_endpoint_descriptor_t *)header;
#if TRACE
                const char* direction = (endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
                                         == USB_ENDPOINT_IN ? "IN" : "OUT";
#endif
                xprintf("USB_DT_ENDPOINT %s bmAttributes: 0x%02X\n", direction, endp->bmAttributes);

                if (intf) {
                    if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_STREAMING &&
                        usb_ep_type(endp) == USB_ENDPOINT_ISOCHRONOUS) {
                        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
                            usb_audio_sink_create(device, &usb, audio_sink_index++, intf, endp,
                                                  format_desc);
                        } else {
                             usb_audio_source_create(device, &usb, audio_source_index++, intf, endp,
                                                     format_desc);
                        }
                        // this is a quick and dirty hack to set volume to 100%
                        // otherwise, audio might default to 0%
                        //
                        // TODO(johngro): Rework all of this code.  USB audio devices are very much
                        // like HDA codecs; Internally, they are made up of a graph of nodes
                        // (terminals and units) with a bunch of different possible topologies.
                        // Simply setting the volume controls (when present) in each discovered
                        // Feature Unit to 100% will not guarantee that we will get a useful flow of
                        // audio through the system.  It is possible that selectors, or mixers (with
                        // their own gains) will need to be configured in order to properly pass
                        // audio as well.  In addition, by taking the shotgun approach with the
                        // Feature Units here, we might end up accidentally looping back microphone
                        // input into headphone/speaker output at 100% gain.  Normally topologies
                        // like this are intended to provide an analog sidetone for headsets, which
                        // we would generally want to be off, or only a small amount of gain when
                        // sidetone should be enabled.
                        //
                        // Moving forward, we should probably put another level into the hierarchy
                        // of devices published here.  Instead of publishing streams directly, we
                        // should start by publishing a control node which represents the audio
                        // control interface discovered here.  This control node device can then
                        // read the Terminal/Unit descriptors to build the graph which represents
                        // the device topology.  Then it can identify the paths through the graph
                        // that we want to expose as input and output streams to the rest of the
                        // system.  Once that is done, it can publish stream devices as child
                        // devices based on the discovered paths.  Eventually, we might even make
                        // this sophisticated enough that we define an interface for the control
                        // node device so that the system can dynamically reconfigure the graph
                        // (when appropriate/possible) in ways which might result in
                        // publishing/unpublishing stream devices.
                        feature_unit_node_t* fu_node;
                        list_for_every_entry(&fu_descs, fu_node, feature_unit_node_t, node) {
                            // this may fail, but we are taking shotgun approach here
                            usb_audio_set_volume(&usb, fu_node->intf_num, fu_node->desc, 100);
                        }
                    } else if (intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING &&
                        usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
                           usb_midi_sink_create(device, &usb, midi_sink_index++, intf, endp);
                        } else {
                           usb_midi_source_create(device, &usb, midi_source_index++, intf, endp);
                        }
                    }
                }
                break;
        }
        case USB_AUDIO_CS_DEVICE:
            xprintf("USB_AUDIO_CS_DEVICE\n");
            break;
        case USB_AUDIO_CS_CONFIGURATION:
            xprintf("USB_AUDIO_CS_CONFIGURATION\n");
            break;
        case USB_AUDIO_CS_STRING:
            xprintf("USB_AUDIO_CS_STRING\n");
            break;
        case USB_AUDIO_CS_INTERFACE: {
            usb_audio_ac_desc_header* ac_header = (usb_audio_ac_desc_header *)header;
            if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_CONTROL) {
                switch (ac_header->bDescriptorSubtype) {
                case USB_AUDIO_AC_HEADER:
                    xprintf("USB_AUDIO_AC_HEADER\n");
                    break;
                case USB_AUDIO_AC_INPUT_TERMINAL: {
#if TRACE
                    usb_audio_ac_input_terminal_desc* desc = (usb_audio_ac_input_terminal_desc *)header;
#endif
                    xprintf("USB_AUDIO_AC_INPUT_TERMINAL wTerminalType: %04X\n",
                            le16toh(desc->wTerminalType));
                    break;
                }
                case USB_AUDIO_AC_OUTPUT_TERMINAL: {
#if TRACE
                    usb_audio_ac_output_terminal_desc* desc = (usb_audio_ac_output_terminal_desc *)header;
#endif
                    xprintf("USB_AUDIO_AC_OUTPUT_TERMINAL wTerminalType: %04X\n",
                            le16toh(desc->wTerminalType));
                    break;
                }
                case USB_AUDIO_AC_MIXER_UNIT:
                    xprintf("USB_AUDIO_AC_MIXER_UNIT\n");
                    break;
                case USB_AUDIO_AC_SELECTOR_UNIT:
                    xprintf("USB_AUDIO_AC_SELECTOR_UNIT\n");
                    break;
                case USB_AUDIO_AC_FEATURE_UNIT: {
                    xprintf("USB_AUDIO_AC_FEATURE_UNIT\n");
                    feature_unit_node_t* fu_node = malloc(sizeof(feature_unit_node_t));
                    if (fu_node) {
                        fu_node->desc = (usb_audio_ac_feature_unit_desc *)header;
                        fu_node->intf_num = intf->bInterfaceNumber;
                        list_add_tail(&fu_descs, &fu_node->node);
#if TRACE
                        usb_audio_dump_feature_unit_caps(&usb,
                                                         intf->bInterfaceNumber,
                                                         fu_node->desc);
#endif
                    }
                    break;
                }
                case USB_AUDIO_AC_PROCESSING_UNIT:
                    xprintf("USB_AUDIO_AC_PROCESSING_UNIT\n");
                    break;
                case USB_AUDIO_AC_EXTENSION_UNIT:
                    xprintf("USB_AUDIO_AS_FORMAT_TYPE\n");
                    break;
                }
            } else if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_STREAMING) {
                switch (ac_header->bDescriptorSubtype) {
                case USB_AUDIO_AS_GENERAL:
                    xprintf("USB_AUDIO_AS_GENERAL\n");
                    break;
                case USB_AUDIO_AS_FORMAT_TYPE: {
                    usb_audio_ac_format_type_i_desc* desc = (usb_audio_ac_format_type_i_desc *)header;
                    xprintf("USB_AUDIO_AS_FORMAT_TYPE %d\n", desc->bFormatType);
                    if (desc->bFormatType == USB_AUDIO_FORMAT_TYPE_I) {
                        format_desc = desc;
                    }
                    break;
                }
                }
            } else if (intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING) {
                switch (ac_header->bDescriptorSubtype) {
                case USB_MIDI_MS_HEADER:
                    xprintf("USB_MIDI_MS_HEADER\n");
                    break;
                case USB_MIDI_IN_JACK:
                    xprintf("USB_MIDI_IN_JACK\n");
                    break;
                case USB_MIDI_OUT_JACK:
                    xprintf("USB_MIDI_OUT_JACK\n");
                    break;
                case USB_MIDI_ELEMENT:
                    xprintf("USB_MIDI_ELEMENT\n");
                    break;
                }
            }
            break;
        }
        case USB_AUDIO_CS_ENDPOINT: {
#if TRACE
            usb_audio_ac_desc_header* ac_header = (usb_audio_ac_desc_header *)header;
#endif
            xprintf("USB_AUDIO_CS_ENDPOINT subtype %d\n", ac_header->bDescriptorSubtype);
            break;
        }
        default:
            xprintf("unknown DT %d\n", header->bDescriptorType);
            break;
        }
    }

    feature_unit_node_t* fu_node;
    while ((fu_node = list_remove_head_type(&fu_descs, feature_unit_node_t, node)) != NULL) {
        free(fu_node);
    }
    usb_desc_iter_release(&iter);

    return ZX_OK;
}

extern void usb_audio_driver_release(void*);

static zx_driver_ops_t usb_audio_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_audio_bind,
    .release = usb_audio_driver_release,
};

ZIRCON_DRIVER_BEGIN(usb_audio, usb_audio_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_USB_CLASS, USB_CLASS_AUDIO),
    BI_ABORT_IF(NE, BIND_USB_SUBCLASS, USB_SUBCLASS_AUDIO_CONTROL),
    BI_MATCH_IF(EQ, BIND_USB_PROTOCOL, 0),
ZIRCON_DRIVER_END(usb_audio)
