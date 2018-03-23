// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dispatcher-pool/dispatcher-thread-pool.h>
#include <string.h>

#include "usb-audio.h"
#include "usb-audio-device.h"
#include "usb-audio-stream.h"

namespace audio {
namespace usb {

// for list of feature unit descriptors
typedef struct {
    list_node_t node;
    usb_audio_ac_feature_unit_desc* desc;
    uint8_t intf_num;
} feature_unit_node_t;

zx_status_t UsbAudioDevice::DriverBind(zx_device_t* parent) {
    fbl::AllocChecker ac;
    auto usb_device = fbl::AdoptRef(new (&ac) audio::usb::UsbAudioDevice(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = usb_device->Bind();
    if (status != ZX_OK) {
        return status;
    }

    // We have transfered our fbl::RefPtr reference to the C ddk.  We will
    // recover it (someday) when the release hook is called.  Until then, we
    // need to deliberately leak our reference so that we do not destruct as we
    // exit this function.
    __UNUSED UsbAudioDevice* leaked_ref;
    leaked_ref = usb_device.leak_ref();
    return status;
}

UsbAudioDevice::UsbAudioDevice(zx_device_t* parent) : UsbAudioDeviceBase(parent) {
    ::memset(&usb_proto_, 0, sizeof(usb_proto_));
    ::memset(&usb_dev_desc_, 0, sizeof(usb_dev_desc_));
    snprintf(log_prefix_, sizeof(log_prefix_), "UsbAud Unknown");
}

zx_status_t UsbAudioDevice::Bind() {
    zx_status_t status;

    status = device_get_protocol(parent(), ZX_PROTOCOL_USB, &usb_proto_);
    if (status != ZX_OK) {
        LOG(ERROR, "Failed to get USB protocol thunks (status %d)\n", status);
        return status;
    }

    usb_get_device_descriptor(&usb_proto_, &usb_dev_desc_);
    snprintf(log_prefix_, sizeof(log_prefix_), "UsbAud %04x:%04x", vid(), pid());

    status = usb_claim_additional_interfaces(
            &usb_proto_,
            [](usb_interface_descriptor_t* intf, void* arg) -> bool {
                return (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                        intf->bInterfaceSubClass != USB_SUBCLASS_AUDIO_CONTROL);
            },
            NULL);
    if (status != ZX_OK) {
        return status;
    }

    status = DdkAdd("usb-audio-ctrl");
    if (status != ZX_OK) {
        return status;
    }

    Probe();

    return ZX_OK;
}

void UsbAudioDevice::Probe() {
    // find our endpoints
    usb_desc_iter_t iter;
    zx_status_t status;

    status = usb_desc_iter_init(&usb_proto_, &iter);
    if (status < 0) {
        LOG(WARN, "Failed to fetch descriptor iterator (status %d)\n", status);
        return;
    }

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
                        LOG(TRACE, "interface USB_SUBCLASS_AUDIO_CONTROL\n");
                        break;
                    } else if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_STREAMING) {
                        LOG(TRACE, "interface USB_SUBCLASS_AUDIO_STREAMING bAlternateSetting: %d\n",
                                intf->bAlternateSetting);
                        // reset format and feature unit descriptors
                        format_desc = NULL;
                        break;
                    } else if (intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING) {
                        LOG(TRACE, "interface USB_SUBCLASS_MIDI_STREAMING bAlternateSetting: %d\n",
                                intf->bAlternateSetting);
                        break;
                    }
                }
                LOG(TRACE, "USB_DT_INTERFACE %d %d %d\n", intf->bInterfaceClass, intf->bInterfaceSubClass,
                        intf->bInterfaceProtocol);
                break;
        }
        case USB_DT_ENDPOINT: {
                usb_endpoint_descriptor_t* endp = (usb_endpoint_descriptor_t *)header;
                const char* direction = (endp->bEndpointAddress & USB_ENDPOINT_DIR_MASK)
                                         == USB_ENDPOINT_IN ? "IN" : "OUT";
                LOG(TRACE, "USB_DT_ENDPOINT %s bmAttributes: 0x%02X\n", direction, endp->bmAttributes);

                if (intf) {
                    if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_STREAMING &&
                        usb_ep_type(endp) == USB_ENDPOINT_ISOCHRONOUS) {
                        bool input = usb_ep_direction(endp) != USB_ENDPOINT_OUT;
                        UsbAudioStream::Create(fbl::WrapRefPtr(this),
                                               input,
                                               &usb_proto_,
                                               input ? audio_source_index++ : audio_sink_index++,
                                               intf,
                                               endp,
                                               format_desc);

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
                            usb_audio_set_volume(&usb_proto_, fu_node->intf_num, fu_node->desc, 100);
                        }
                    } else if (intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING &&
                        usb_ep_type(endp) == USB_ENDPOINT_BULK) {
                        if (usb_ep_direction(endp) == USB_ENDPOINT_OUT) {
                           usb_midi_sink_create(zxdev(), &usb_proto_, midi_sink_index++, intf, endp);
                        } else {
                           usb_midi_source_create(zxdev(), &usb_proto_, midi_source_index++, intf, endp);
                        }
                    }
                }
                break;
        }
        case USB_AUDIO_CS_DEVICE:
            LOG(TRACE, "USB_AUDIO_CS_DEVICE\n");
            break;
        case USB_AUDIO_CS_CONFIGURATION:
            LOG(TRACE, "USB_AUDIO_CS_CONFIGURATION\n");
            break;
        case USB_AUDIO_CS_STRING:
            LOG(TRACE, "USB_AUDIO_CS_STRING\n");
            break;
        case USB_AUDIO_CS_INTERFACE: {
            usb_audio_desc_header* ac_header = (usb_audio_desc_header *)header;
            if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_CONTROL) {
                switch (ac_header->bDescriptorSubtype) {
                case USB_AUDIO_AC_HEADER:
                    LOG(TRACE, "USB_AUDIO_AC_HEADER\n");
                    break;
                case USB_AUDIO_AC_INPUT_TERMINAL: {
                    auto desc = reinterpret_cast<usb_audio_ac_input_terminal_desc*>(header);
                    LOG(TRACE, "USB_AUDIO_AC_INPUT_TERMINAL wTerminalType: %04X\n",
                            le16toh(desc->wTerminalType));
                    break;
                }
                case USB_AUDIO_AC_OUTPUT_TERMINAL: {
                    auto desc = reinterpret_cast<usb_audio_ac_output_terminal_desc*>(header);
                    LOG(TRACE, "USB_AUDIO_AC_OUTPUT_TERMINAL wTerminalType: %04X\n",
                            le16toh(desc->wTerminalType));
                    break;
                }
                case USB_AUDIO_AC_MIXER_UNIT:
                    LOG(TRACE, "USB_AUDIO_AC_MIXER_UNIT\n");
                    break;
                case USB_AUDIO_AC_SELECTOR_UNIT:
                    LOG(TRACE, "USB_AUDIO_AC_SELECTOR_UNIT\n");
                    break;
                case USB_AUDIO_AC_FEATURE_UNIT: {
                    LOG(TRACE, "USB_AUDIO_AC_FEATURE_UNIT\n");
                    auto fu_node =
                        reinterpret_cast<feature_unit_node_t*>(malloc(sizeof(feature_unit_node_t)));
                    if (fu_node) {
                        fu_node->desc = (usb_audio_ac_feature_unit_desc *)header;
                        fu_node->intf_num = intf->bInterfaceNumber;
                        list_add_tail(&fu_descs, &fu_node->node);
                        if (zxlog_level_enabled(TRACE)) {
                            usb_audio_dump_feature_unit_caps(&usb_proto_,
                                                             intf->bInterfaceNumber,
                                                             fu_node->desc);
                        }
                    }
                    break;
                }
                case USB_AUDIO_AC_PROCESSING_UNIT:
                    LOG(TRACE, "USB_AUDIO_AC_PROCESSING_UNIT\n");
                    break;
                case USB_AUDIO_AC_EXTENSION_UNIT:
                    LOG(TRACE, "USB_AUDIO_AS_FORMAT_TYPE\n");
                    break;
                }
            } else if (intf->bInterfaceSubClass == USB_SUBCLASS_AUDIO_STREAMING) {
                switch (ac_header->bDescriptorSubtype) {
                case USB_AUDIO_AS_GENERAL:
                    LOG(TRACE, "USB_AUDIO_AS_GENERAL\n");
                    break;
                case USB_AUDIO_AS_FORMAT_TYPE: {
                    usb_audio_ac_format_type_i_desc* desc = (usb_audio_ac_format_type_i_desc *)header;
                    LOG(TRACE, "USB_AUDIO_AS_FORMAT_TYPE %d\n", desc->bFormatType);
                    if (desc->bFormatType == USB_AUDIO_FORMAT_TYPE_I) {
                        format_desc = desc;
                    }
                    break;
                }
                }
            } else if (intf->bInterfaceSubClass == USB_SUBCLASS_MIDI_STREAMING) {
                switch (ac_header->bDescriptorSubtype) {
                case USB_MIDI_MS_HEADER:
                    LOG(TRACE, "USB_MIDI_MS_HEADER\n");
                    break;
                case USB_MIDI_IN_JACK:
                    LOG(TRACE, "USB_MIDI_IN_JACK\n");
                    break;
                case USB_MIDI_OUT_JACK:
                    LOG(TRACE, "USB_MIDI_OUT_JACK\n");
                    break;
                case USB_MIDI_ELEMENT:
                    LOG(TRACE, "USB_MIDI_ELEMENT\n");
                    break;
                }
            }
            break;
        }
        case USB_AUDIO_CS_ENDPOINT: {
            auto ac_header = reinterpret_cast<usb_audio_desc_header*>(header);
            LOG(TRACE, "USB_AUDIO_CS_ENDPOINT subtype %d\n", ac_header->bDescriptorSubtype);
            break;
        }
        default:
            LOG(TRACE, "unknown DT %d\n", header->bDescriptorType);
            break;
        }
    }

    feature_unit_node_t* fu_node;
    while ((fu_node = list_remove_head_type(&fu_descs, feature_unit_node_t, node)) != NULL) {
        free(fu_node);
    }
    usb_desc_iter_release(&iter);
}

void UsbAudioDevice::DdkUnbind() {
    // Unpublish our device node.
    DdkRemove();
}

void UsbAudioDevice::DdkRelease() {
    // Recover our reference from the unmanaged C DDK.  Then, just let it go out
    // of scope.
    auto reference = fbl::internal::MakeRefPtrNoAdopt(this);
}

}  // namespace usb
}  // namespace audio

extern "C" {
zx_status_t usb_audio_device_bind(void* ctx, zx_device_t* device) {
    return audio::usb::UsbAudioDevice::DriverBind(device);
}

void usb_audio_driver_release(void*) {
    dispatcher::ThreadPool::ShutdownAll();
}
}  // extern "C"
