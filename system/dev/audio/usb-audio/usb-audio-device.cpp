// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dispatcher-pool/dispatcher-thread-pool.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_double_list.h>
#include <string.h>

#include "usb-audio.h"
#include "usb-audio-device.h"
#include "usb-audio-stream.h"
#include "usb-audio-stream-interface.h"

namespace audio {
namespace usb {

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

void UsbAudioDevice::RemoveAudioStream(const fbl::RefPtr<UsbAudioStream>& stream) {
    fbl::AutoLock lock(&lock_);
    ZX_DEBUG_ASSERT(stream != nullptr);
    if (stream->InContainer()) {
        streams_.erase(*stream);
    }
}

zx_status_t UsbAudioDevice::Bind() {
    zx_status_t status;

    // Fetch our protocol.  We will need it to do pretty much anything with this
    // device.
    status = device_get_protocol(parent(), ZX_PROTOCOL_USB, &usb_proto_);
    if (status != ZX_OK) {
        LOG(ERROR, "Failed to get USB protocol thunks (status %d)\n", status);
        return status;
    }

    // Fetch our top level device descriptor, so we know stuff like the values
    // of our VID/PID.
    usb_get_device_descriptor(&usb_proto_, &usb_dev_desc_);
    snprintf(log_prefix_, sizeof(log_prefix_), "UsbAud %04x:%04x", vid(), pid());

    // Attempt to cache the string descriptors for our manufacturer name,
    // product name, and serial number.
    if (usb_dev_desc_.iManufacturer) {
        mfr_name_ = FetchStringDescriptor(usb_proto_, usb_dev_desc_.iManufacturer);
    }

    if (usb_dev_desc_.iProduct) {
        prod_name_ = FetchStringDescriptor(usb_proto_, usb_dev_desc_.iProduct);
    }

    if (usb_dev_desc_.iSerialNumber) {
        serial_num_ = FetchStringDescriptor(usb_proto_, usb_dev_desc_.iSerialNumber);
    }

    // Our top level binding script has only claimed audio interfaces with a
    // subclass of control.  Go ahead and claim anything which has a top level
    // class of of "audio"; this is where we will find our Audio and MIDI
    // streaming interfaces.
    status = usb_claim_additional_interfaces(
            &usb_proto_,
            [](usb_interface_descriptor_t* intf, void* arg) -> bool {
                return (intf->bInterfaceClass == USB_CLASS_AUDIO &&
                        intf->bInterfaceSubClass != USB_SUBCLASS_AUDIO_CONTROL);
            },
            NULL);
    if (status != ZX_OK) {
        LOG(ERROR, "Failed to claim additional audio interfaces (status %d)\n", status);
        return status;
    }

    // Allocate and read in our descriptor list.
    desc_list_ = DescriptorListMemory::Create(&usb_proto_);
    if (desc_list_ == nullptr) {
        LOG(ERROR, "Failed to fetch descriptor list\n");
        return status;
    }

    // Publish our device.
    status = DdkAdd("usb-audio-ctrl");
    if (status != ZX_OK) {
        return status;
    }

    Probe();

    return ZX_OK;
}

void UsbAudioDevice::Probe() {
    // A reference to the audio control interface along with the set of audio
    // stream interfaces that we discover during probing.  We will need at least
    // one control interface and one or more usable streaming audio interface if
    // we want to publish *any* audio streams.
    fbl::unique_ptr<UsbAudioControlInterface> control_ifc;
    fbl::DoublyLinkedList<fbl::unique_ptr<UsbAudioStreamInterface>> aud_stream_ifcs;

    // Go over our descriptor list.  Right now, we are looking for only three
    // things; The Audio Control interface, and the various Audio/MIDI Streaming
    // interfaces.
    DescriptorListMemory::Iterator iter(desc_list_);
    while (iter.valid()) {
        // Advance to the next descriptor if we don't find and parse an
        // interface we understand.
        auto cleanup = fbl::MakeAutoCall([&iter] { iter.Next(); });
        auto hdr = iter.hdr();

        // We are only prepared to find interface descriptors at this point.
        if (hdr->bDescriptorType != USB_DT_INTERFACE) {
            LOG(WARN, "Skipping unexpected descriptor (len = %u, type = %u)\n",
                hdr->bLength, hdr->bDescriptorType);
            continue;
        }

        auto ihdr = iter.hdr_as<usb_interface_descriptor_t>();
        if (ihdr == nullptr) {
            LOG(WARN, "Skipping bad interface descriptor header @ offset %zu/%zu\n",
                iter.offset(), iter.desc_list()->size());
            continue;
        }

        if ((ihdr->bInterfaceClass != USB_CLASS_AUDIO) ||
           ((ihdr->bInterfaceSubClass != USB_SUBCLASS_AUDIO_CONTROL) &&
            (ihdr->bInterfaceSubClass != USB_SUBCLASS_AUDIO_STREAMING) &&
            (ihdr->bInterfaceSubClass != USB_SUBCLASS_MIDI_STREAMING))) {
            LOG(WARN, "Skipping unknown interface (class %u, subclass %u)\n",
                ihdr->bInterfaceClass, ihdr->bInterfaceSubClass);
            continue;
        }

        switch (ihdr->bInterfaceSubClass) {
        case USB_SUBCLASS_AUDIO_CONTROL: {
            if (control_ifc != nullptr) {
                LOG(WARN, "More than one audio control interface detected, skipping.\n");
                break;
            }

            auto control = UsbAudioControlInterface::Create(this);
            if (control == nullptr) {
                LOG(WARN, "Failed to allocate audio control interface\n");
                break;
            }

            // Give the control interface a chance to parse it's contents.
            // Success or failure, when we are finished, the iterator should
            // have been advanced to the next descriptor which does not make
            // sense to the control interface parser.  Cancel the cleanup
            // task so that it does not skip over this header.
            zx_status_t res = control->Initialize(&iter);
            cleanup.cancel();
            if (res == ZX_OK) {
                // No need to log in case of failure, the interface class
                // should already have done so.
                control_ifc = fbl::move(control);
            }
            break;
        }

        case USB_SUBCLASS_AUDIO_STREAMING: {
            // We recognize this header and are going to consume it (whether or
            // not we successfully create or add to an existing audio stream
            // interface).  Cancel the cleanup lambda so that it does not skip
            // the next header as well.
            cleanup.cancel();

            // Check to see if this is a new interface, or an alternate
            // interface description for an existing stream interface.
            uint8_t iid = ihdr->bInterfaceNumber;
            auto ifc_iter = aud_stream_ifcs.find_if(
                [iid](const UsbAudioStreamInterface& ifc) -> bool { return ifc.iid() == iid; });

            if (ifc_iter.IsValid()) {
                zx_status_t res = ifc_iter->AddInterface(&iter);
                if (res != ZX_OK) {
                    LOG(WARN, "Failed to add audio stream interface (id %u) @ offset %zu/%zu\n",
                        iid, iter.offset(), iter.desc_list()->size());
                }
            } else {
                auto ifc = UsbAudioStreamInterface::Create(this, &iter);
                if (ifc == nullptr) {
                    LOG(WARN,
                        "Failed to create audio stream interface (id %u) @ offset %zu/%zu\n",
                        iid, iter.offset(), iter.desc_list()->size());
                } else {
                    LOG(TRACE, "Discovered new audio streaming interface (id %u)\n", iid);
                    aud_stream_ifcs.push_back(fbl::move(ifc));
                }
            }
            break;
        }

        // TODO(johngro): Do better than this for MIDI streaming interfaces.
        // We should probably mirror the pattern used for the audio
        // streaming interfaces where we create a class to hold all of the
        // interfaces along with their descriptors and alternate interface
        // variants, then pass that class on to a driver class assuming that
        // everything checks out.
        //
        // Right now, we just look for a top level interface descriptor
        // along with a single endpoint descriptor, and skip pretty much
        // everything else.
        case USB_SUBCLASS_MIDI_STREAMING: {
            // We recognize this header and are going to consume it (whether or
            // not we successfully create or add to an existing audio stream
            // interface).  Cancel the cleanup lambda so that it does not skip
            // the next header as well.
            cleanup.cancel();

            // Go looking for the endpoint descriptor which goes with this
            // streaming descriptor.  If we find one, attempt to publish a device.
            struct MidiStreamingInfo info(ihdr);
            ParseMidiStreamingIfc(&iter, &info);

            if (info.out_ep != nullptr) {
                LOG(TRACE, "Adding MIDI sink (iid %u, ep 0x%02x)\n",
                    info.ifc->bInterfaceNumber, info.out_ep->bEndpointAddress);
                usb_midi_sink_create(zxdev(),
                                     &usb_proto_,
                                     midi_sink_index_++,
                                     info.ifc,
                                     info.out_ep);
            }

            if (info.in_ep != nullptr) {
                LOG(TRACE, "Adding MIDI source (iid %u, ep 0x%02x)\n",
                    info.ifc->bInterfaceNumber, info.in_ep->bEndpointAddress);
                usb_midi_source_create(zxdev(),
                                     &usb_proto_,
                                     midi_source_index_++,
                                     info.ifc,
                                     info.in_ep);
            }

            break;
        }  // case
        }  // switch
    }

    if ((control_ifc == nullptr) && !aud_stream_ifcs.is_empty()) {
        LOG(WARN, "No control interface discovered.  Discarding all audio streaming interfaces\n");
        aud_stream_ifcs.clear();
    }

    // Now that we are done parsing all of our descriptors, go over our list of
    // audio streaming interfaces and pair each up with the appropriate audio
    // path as we go.  Create an actual Fuchsia audio stream for each valid
    // streaming interface with a a valid audio path.
    while (!aud_stream_ifcs.is_empty()) {
        // Build the format map for this stream interface.  If we cannot find
        // any usable formats for this streaming interface, simply discard it.
        auto stream_ifc = aud_stream_ifcs.pop_front();
        zx_status_t status = stream_ifc->BuildFormatMap();
        if (status != ZX_OK) {
            LOG(ERROR, "Failed to build format map for streaming interface id %u (status %d)\n",
                stream_ifc->iid(), status);
            continue;
        }

        // Find the path which goes with this interface.
        auto path = control_ifc->ExtractPath(stream_ifc->term_link(), stream_ifc->direction());
        if (path == nullptr) {
            LOG(WARN,
                "Discarding audio streaming interface (id %u) as we could not find a path to match "
                "its terminal link ID (%u) and direction (%u)\n",
                stream_ifc->iid(),
                stream_ifc->term_link(),
                static_cast<uint32_t>(stream_ifc->direction()));
            continue;
        }

        // Link the path to the stream interface.
        LOG(TRACE, "Linking streaming interface id %u to audio path terminal %u\n",
                stream_ifc->iid(), path->stream_terminal().id());
        stream_ifc->LinkPath(fbl::move(path));

        // Log a warning if we are about to build an audio path which operates
        // in separate clock domain.  We still need to add support for this
        // case, see ZX-2044 for details.
        if (stream_ifc->ep_sync_type() == EndpointSyncType::Async) {
            LOG(WARN,
                "Warning: Creating USB audio %s with operating in Asynchronous Isochronous mode. "
                "See ZX-2044\n",
                stream_ifc->direction() == Direction::Input ? "input" : "output");
        }

        // Create a new audio stream, handing the stream interface over to it.
        auto stream = UsbAudioStream::Create(this, fbl::move(stream_ifc));
        if (stream == nullptr) {
            // No need to log an error, the Create method has already done so.
            continue;
        }

        // Make sure that the stream is being tracked in our streams_ collection
        // before attemping to publish its device node.
        {
            fbl::AutoLock lock(&lock_);
            streams_.push_back(stream);
        }

        // Publish the new stream.  If something goes wrong, take it out of the
        // streams_ collection.
        status = stream->Bind();
        if (status != ZX_OK) {
            // Again, no need to log.  Bind will have already logged any error.
            RemoveAudioStream(stream);
        }
    }
}

void UsbAudioDevice::ParseMidiStreamingIfc(DescriptorListMemory::Iterator* iter,
                                           MidiStreamingInfo* inout_info) {
    MidiStreamingInfo& info = *inout_info;

    ZX_DEBUG_ASSERT(iter != nullptr);
    ZX_DEBUG_ASSERT(inout_info != nullptr);
    ZX_DEBUG_ASSERT(inout_info->ifc != nullptr);

    // Go looking for the endpoint descriptor which goes with this
    // streaming descriptor.  Try to consume all of the descriptors
    // which go with this MIDI streaming descriptor as we go.
    while (iter->Next()) {
        auto hdr = iter->hdr();

        switch (hdr->bDescriptorType) {
        // Generic interface
        case USB_DT_INTERFACE: {
            auto ihdr = iter->hdr_as<usb_interface_descriptor_t>();
            if (ihdr == nullptr) {
                return;
            }

            // If this is not a midi streaming interface, or it is a midi
            // streaming interface with a different interface id, than the ones
            // we have been seeing, then we are done.
            if ((ihdr->bInterfaceSubClass != USB_SUBCLASS_MIDI_STREAMING) ||
                (ihdr->bInterfaceNumber != info.ifc->bInterfaceNumber)) {
                return;
            }

            // If we have already found an endpoint which goes with an
            // interface, then this is another alternate setting (either
            // with an endpoint or an idle alternate settings).  In a
            // more complicated world, we should handle this, but for
            // now we just log a warning and skip it.
            if ((info.out_ep != nullptr) || (info.in_ep != nullptr)) {
                LOG(WARN,
                    "Multiple alternate settings found for MIDI streaming interface "
                    "(iid %u, alt %u)\n",
                    ihdr->bInterfaceNumber,
                    ihdr->bAlternateSetting);
                continue;
            }

            // Stash this as the most recent MIDI streaming interface we have
            // discovered, and keep parsing looking for the associated
            // endpoint(s).
            info.ifc = ihdr;
        } break;

        // Class specific interface
        case USB_AUDIO_CS_INTERFACE: {
            auto aud_hdr = iter->hdr_as<usb_audio_desc_header>();
            if (aud_hdr == nullptr) {
                return;
            }

            // Silently skip the class specific MIDI headers which go
            // along with this streaming interface descriptor.
            if ((aud_hdr->bDescriptorSubtype == USB_MIDI_MS_HEADER) ||
                (aud_hdr->bDescriptorSubtype == USB_MIDI_IN_JACK) ||
                (aud_hdr->bDescriptorSubtype == USB_MIDI_OUT_JACK) ||
                (aud_hdr->bDescriptorSubtype == USB_MIDI_ELEMENT)) {
                LOG(SPEW, "Skipping class specific MIDI interface subtype = %u\n",
                    aud_hdr->bDescriptorSubtype);
                continue;
            }

            // We don't recognize this class specific interface header.  Stop
            // parsing.
            return;
        } break;

        // Generic Endpoint
        case USB_DT_ENDPOINT: {
            auto ep_desc = iter->hdr_as<usb_endpoint_descriptor_t>();
            if (ep_desc == nullptr) {
                return;
            }

            // If this is not a bulk transfer endpoint, then we are not quite sure what to do with
            // it.  Log a warning and skip it.
            if (usb_ep_type(ep_desc) != USB_ENDPOINT_BULK) {
                LOG(WARN,
                    "Skipping Non-bulk transfer endpoint (%u) found for MIDI streaming interface "
                    "(iid %u, alt %u)\n",
                    usb_ep_type(ep_desc),
                    info.ifc->bInterfaceNumber,
                    info.ifc->bAlternateSetting);
                continue;
            }

            auto& ep_tgt = (usb_ep_direction(ep_desc) == USB_ENDPOINT_OUT)
                         ? info.out_ep
                         : info.in_ep;
            const char* log_tag = (usb_ep_direction(ep_desc) == USB_ENDPOINT_OUT)
                        ? "output" : "input";

            // If we have already found an endpoint for this interface, log a
            // warning and skip this one.
            if (ep_tgt != nullptr) {
                LOG(WARN,
                    "Multiple %s endpoints found found for MIDI streaming interface "
                    "(iid %u, alt %u, exiting ep_addr 0x%02x, new ep_addr 0x%02x)\n",
                    log_tag,
                    info.ifc->bInterfaceNumber,
                    info.ifc->bAlternateSetting,
                    ep_tgt->bEndpointAddress,
                    ep_desc->bEndpointAddress);
                continue;
            }

            // Stash this endpoint as the found endpoint and keep parsing to
            // consume the rest of the descriptors associated with this
            // interface that we plan to ignore.
            LOG(SPEW, "Found %s MIDI endpoint descriptor (addr 0x%02x, attr 0x%02x)\n",
                log_tag, ep_desc->bEndpointAddress, ep_desc->bmAttributes);
            ep_tgt = ep_desc;
        } break;

        case USB_AUDIO_CS_ENDPOINT: {
            auto ep_desc = iter->hdr_as<usb_midi_ms_endpoint_desc>();
            if (ep_desc == nullptr) {
                return;
            }

            if (ep_desc->bDescriptorSubtype == USB_MIDI_MS_GENERAL) {
                LOG(SPEW, "Skipping class specific MIDI endpoint\n");
                continue;
            }

            return;
        } break;

        default:
            return;
        }
    }
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
