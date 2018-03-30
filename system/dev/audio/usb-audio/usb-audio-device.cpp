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

        switch (hdr->bDescriptorType) {
        case USB_DT_INTERFACE: {
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
                // We recognize this header and are going to consume it (whether
                // or not we successfully create or add to an existing stream
                // interface).  Cancel the cleanup lambda so that it does not
                // skip the next header as well.
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

            case USB_SUBCLASS_MIDI_STREAMING:
                break;
            }

        } break;

        default:
            LOG(WARN, "Skipping unexpected descriptor (len = %u, type = %u)\n",
                hdr->bLength, hdr->bDescriptorType);
            iter.Next();
            break;
        }
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
