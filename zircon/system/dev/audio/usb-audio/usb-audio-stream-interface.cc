// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-audio-stream-interface.h"

#include <memory>
#include <utility>

#include <audio-proto-utils/format-utils.h>
#include <fbl/algorithm.h>
#include <fbl/auto_call.h>

#include "debug-logging.h"
#include "usb-audio-device.h"
#include "usb-audio-path.h"

namespace audio {
namespace usb {

// We use our parent's log prefix
const char* UsbAudioStreamInterface::log_prefix() const { return parent_.log_prefix(); }

std::unique_ptr<UsbAudioStreamInterface> UsbAudioStreamInterface::Create(
    UsbAudioDevice* parent, DescriptorListMemory::Iterator* iter) {
  ZX_DEBUG_ASSERT(parent != nullptr);
  ZX_DEBUG_ASSERT(iter != nullptr);

  auto ihdr = iter->hdr_as<usb_interface_descriptor_t>();
  ZX_DEBUG_ASSERT(ihdr);  // The caller should have already verified this.
  uint8_t iid = ihdr->bInterfaceNumber;

  fbl::AllocChecker ac;
  std::unique_ptr<UsbAudioStreamInterface> ret(
      new (&ac) UsbAudioStreamInterface(parent, iter->desc_list(), iid));
  if (ac.check()) {
    zx_status_t res = ret->AddInterface(iter);
    if (res == ZX_OK) {
      return ret;
    }
    LOG_EX(ERROR, *parent,
           "Failed to add initial interface (id %u) to UsbAudioStreamInterface (res %d)\n", iid,
           res);
  } else {
    iter->Next();  // Success or failure, we are expected to consume this header.
    LOG_EX(ERROR, *parent, "Out of memory attempting to allocate UsbAudioStreamInterface (id %u)\n",
           iid);
  }

  return nullptr;
}

zx_status_t UsbAudioStreamInterface::AddInterface(DescriptorListMemory::Iterator* iter) {
  // All of these checks should have been made by the caller already.
  ZX_DEBUG_ASSERT(iter != nullptr);
  ZX_DEBUG_ASSERT(iter->desc_list() == desc_list_);

  auto ihdr = iter->hdr_as<usb_interface_descriptor_t>();
  ZX_DEBUG_ASSERT(ihdr != nullptr);
  ZX_DEBUG_ASSERT(ihdr->bInterfaceNumber == iid());

  // No matter what, we need to consume the current descriptor header.
  iter->Next();

  // Make sure that this header represents a unique alternate setting.
  auto alt_id = ihdr->bAlternateSetting;
  auto fmt_iter =
      formats_.find_if([alt_id](const Format& fmt) -> bool { return alt_id == fmt.alt_id(); });
  if (fmt_iter.IsValid() || ((idle_hdr_ && (idle_hdr_->bAlternateSetting == alt_id)))) {
    LOG(WARN,
        "Skipping duplicate alternate setting ID in streaming interface descriptor.  "
        "(iid %u, alt_id %u)\n",
        ihdr->bInterfaceNumber, alt_id);
    // Don't return an error if we encounter a malformed header.  Just skip
    // it and do the best we can with what we have.
    return ZX_OK;
  }

  // Examine the next descriptor.  If it is an audio streaming class specific
  // interface descriptor, then this top level descriptor is part of a
  // described format.  Otherwise, this is an empty alternate interface which
  // is probably meant to be selected when this streaming interface is idle
  // and should not be using any bus resources.
  auto next_hdr = iter->hdr_as<usb_audio_desc_header>();
  if ((next_hdr != nullptr) && (next_hdr->bDescriptorType == USB_AUDIO_CS_INTERFACE) &&
      (next_hdr->bDescriptorSubtype == USB_AUDIO_AS_GENERAL)) {
    auto aud_hdr = iter->hdr_as<usb_audio_as_header_desc>();
    iter->Next();

    if (aud_hdr == nullptr) {
      LOG(WARN,
          "Skipping badly formed alternate setting ID in streaming interface descriptor "
          "(iid %u, alt_id %u).\n",
          ihdr->bInterfaceNumber, alt_id);
      return ZX_OK;
    }

    fbl::AllocChecker ac;
    auto format = fbl::make_unique_checked<Format>(&ac, this, iter->desc_list(), ihdr, aud_hdr);
    if (!ac.check()) {
      LOG(ERROR, "Out of memory attempt to add Format to StreamInterface\n");
      return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = format->Init(iter);
    if (status != ZX_OK) {
      LOG(WARN, "Skipping bad format streaming interface descriptor.  (iid %u, alt_id %u)\n",
          ihdr->bInterfaceNumber, alt_id);
      return ZX_OK;
    }

    // Make sure that the endpoint address and terminal link ID of this
    // format matches all previously encountered formats.
    //
    // TODO(johngro) : It is unclear whether or not it makes any sense to
    // have formats which link to different audio paths or have different
    // endpoint addresses (implying potentially different directions).  For
    // now we simply skip these formats if we encounter them.
    //
    // If we ever encounter a device which has a mix of these parameters, we
    // need come back and determine if there is a good generic approach for
    // dealing with the situation.
    if (!formats_.is_empty()) {
      if (format->term_link() != term_link_) {
        LOG(WARN,
            "Skipping format (iid %u, alt_id %u) with non-uniform terminal ID "
            "(expected %u, got %u)\n",
            ihdr->bInterfaceNumber, alt_id, term_link_, format->term_link());
        return ZX_OK;
      }

      if ((format->ep_addr() != ep_addr_) || (format->ep_attr() != ep_attr_)) {
        LOG(ERROR,
            "Skipping format (iid %u, alt_id %u) with non-uniform endpoint "
            "address/attributes (expected 0x%02x/0x%02x, got 0x%02x/0x%02x)\n",
            ihdr->bInterfaceNumber, alt_id, ep_addr_, ep_attr_, format->ep_addr(),
            format->ep_attr());
        return ZX_OK;
      }
    } else {
      term_link_ = format->term_link();
      ep_addr_ = format->ep_addr();
      ep_attr_ = format->ep_attr();
    }

    max_req_size_ = fbl::max(max_req_size_, format->max_req_size());
    formats_.push_back(std::move(format));
  } else {
    if (idle_hdr_ == nullptr) {
      idle_hdr_ = ihdr;
    } else {
      LOG(WARN,
          "Skipping duplicate \"idle\" interface descriptor in streaming interface "
          "descriptor.  (iid %u, alt_id %u)\n",
          ihdr->bInterfaceNumber, ihdr->bAlternateSetting);
    }
  }

  return ZX_OK;
}

zx_status_t UsbAudioStreamInterface::BuildFormatMap() {
  if (format_map_.size()) {
    LOG(WARN, "Attempted to re-build format map for streaming interface (iid %u)\n", iid());
    return ZX_ERR_BAD_STATE;
  }

  // Make a pass over our list of formats and figure out how big our format
  // map vector may need to be.
  //
  // Note: this is a rough worst case bound on how big the vector needs to be.
  // Someday, we could come back here and compute a much tighter bound if we
  // wanted to.
  size_t worst_case_map_entries = 0;
  for (const auto& fmt : formats_) {
    // A frame rate count of 0 indicates a continuous format range which
    // requires only one format range entry.
    worst_case_map_entries += fmt.frame_rate_cnt() ? fmt.frame_rate_cnt() : 1;
  }

  // Now reserve our memory.
  fbl::AllocChecker ac;
  format_map_.reserve(worst_case_map_entries, &ac);
  if (!ac.check()) {
    LOG(ERROR, "Out of memory attempting to reserve %zu format ranges\n", worst_case_map_entries);
    return ZX_ERR_NO_MEMORY;
  }

  // Now iterate over our set and build the map.
  for (const auto& fmt : formats_) {
    // Record the min/max number of channels.
    audio_stream_format_range_t range;
    range.min_channels = fmt.ch_count();
    range.max_channels = fmt.ch_count();

    // Encode the sample container type from the type I format descriptor
    // as an audio device driver audio_sample_format_t.  If we encounter
    // anything that we don't know how to encode, log a warning and skip the
    // format.
    //
    auto tag = fmt.format_tag();
    if (tag == USB_AUDIO_AS_FT_PCM8) {
      if ((fmt.bit_resolution() != 8) || (fmt.subframe_bytes() != 1)) {
        LOG(WARN, "Skipping PCM8 format with invalid bit res/subframe size (%u/%u)\n",
            fmt.bit_resolution(), fmt.subframe_bytes());
        continue;
      }
      range.sample_formats = static_cast<audio_sample_format_t>(AUDIO_SAMPLE_FORMAT_8BIT |
                                                                AUDIO_SAMPLE_FORMAT_FLAG_UNSIGNED);
    } else if (tag == USB_AUDIO_AS_FT_IEEE_FLOAT) {
      if ((fmt.bit_resolution() != 32) || (fmt.subframe_bytes() != 4)) {
        LOG(WARN, "Skipping IEEE_FLOAT format with invalid bit res/subframe size (%u/%u)\n",
            fmt.bit_resolution(), fmt.subframe_bytes());
        continue;
      }
      range.sample_formats = AUDIO_SAMPLE_FORMAT_32BIT_FLOAT;
    } else if (tag == USB_AUDIO_AS_FT_PCM) {
      switch (fmt.bit_resolution()) {
        case 8:
        case 16:
        case 32: {
          if (fmt.subframe_bytes() != (fmt.bit_resolution() >> 3)) {
            LOG(WARN,
                "Skipping PCM format.  Subframe size (%u bytes) does not "
                "match Bit Res (%u bits)\n",
                fmt.bit_resolution(), fmt.subframe_bytes());
            continue;
          }
          switch (fmt.bit_resolution()) {
            case 8:
              range.sample_formats = AUDIO_SAMPLE_FORMAT_8BIT;
              break;
            case 16:
              range.sample_formats = AUDIO_SAMPLE_FORMAT_16BIT;
              break;
            case 32:
              range.sample_formats = AUDIO_SAMPLE_FORMAT_32BIT;
              break;
          }
        } break;

        case 20:
        case 24: {
          if ((fmt.subframe_bytes() != 3) && (fmt.subframe_bytes() != 4)) {
            LOG(WARN,
                "Skipping PCM format.  %u-bit audio must be packed into a 3 "
                "or 4 byte subframe (Subframe size %u)\n",
                fmt.bit_resolution(), fmt.subframe_bytes());
            continue;
          }
          switch (fmt.bit_resolution()) {
            case 20:
              range.sample_formats = ((fmt.subframe_bytes() == 3) ? AUDIO_SAMPLE_FORMAT_20BIT_PACKED
                                                                  : AUDIO_SAMPLE_FORMAT_20BIT_IN32);
              break;
            case 24:
              range.sample_formats = ((fmt.subframe_bytes() == 3) ? AUDIO_SAMPLE_FORMAT_24BIT_PACKED
                                                                  : AUDIO_SAMPLE_FORMAT_24BIT_IN32);
              break;
          }
        } break;

        default:
          LOG(WARN, "Skipping PCM format with unsupported bit res (%u bits)\n",
              fmt.bit_resolution());
          continue;
      }
    } else {
      LOG(WARN, "Skipping unsupported format tag (%u)\n", tag);
      continue;
    }

    // Now pack the supported frame rates.  A format with a frame rate count of
    // 0 is a continuous range of frame rates.  Otherwise, we pack each discrete
    // frame rate as an individual entry.
    //
    // TODO(johngro) : Discrete frame rates could be encoded more compactly
    // if wanted to do so by extracting all of the 48k and 44.1k rates into
    // a bitmask, and then putting together ranges which represented
    // continuous runs of frame rates in each of the families.
    if (fmt.frame_rate_cnt()) {
      for (uint8_t i = 0; i < fmt.frame_rate_cnt(); ++i) {
        uint32_t rate = fmt.frame_rate(i);
        range.min_frames_per_second = rate;
        range.max_frames_per_second = rate;

        if (audio::utils::FrameRateIn48kFamily(rate)) {
          range.flags = ASF_RANGE_FLAG_FPS_48000_FAMILY;
        } else if (audio::utils::FrameRateIn441kFamily(rate)) {
          range.flags = ASF_RANGE_FLAG_FPS_44100_FAMILY;
        } else {
          range.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;
        }

        format_map_.push_back({range, fmt.alt_id(), fmt.ep_addr(), fmt.max_req_size()});
      }
    } else {
      range.min_frames_per_second = fmt.min_cont_frame_rate();
      range.max_frames_per_second = fmt.max_cont_frame_rate();
      range.flags = ASF_RANGE_FLAG_FPS_CONTINUOUS;
      format_map_.push_back({range, fmt.alt_id(), fmt.ep_addr(), fmt.max_req_size()});
    }
  }

  // If we failed to encode *any* valid format ranges, log a warning and
  // return an error.  This stream interface is not going to be useful to us.
  if (format_map_.is_empty()) {
    LOG(WARN, "Failed to find any usable formats for streaming interface (iid %u)\n", iid());
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t UsbAudioStreamInterface::LookupFormat(uint32_t frames_per_second, uint16_t channels,
                                                  audio_sample_format_t sample_format,
                                                  size_t* out_format_ndx) {
  if (out_format_ndx == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  *out_format_ndx = format_map_.size();

  // Search our format map to find the alternate interface setting which
  // supports the requested format.
  for (size_t i = 0; i < format_map_.size(); ++i) {
    if (audio::utils::FormatIsCompatible(frames_per_second, channels, sample_format,
                                         format_map_[i].range_)) {
      *out_format_ndx = i;
      return ZX_OK;
    }
  }

  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbAudioStreamInterface::ActivateFormat(size_t ndx, uint32_t frames_per_second) {
  if (ndx >= format_map_.size()) {
    return ZX_ERR_INVALID_ARGS;
  }

  // Select the interface used for this format, then configure the endpoint
  // for the requested frame rate.  user know what the maximum request size is
  // for this interface.
  zx_status_t status;
  const auto& f = format_map_[ndx];
  status = usb_set_interface(&parent_.usb_proto(), iid(), f.alt_id_);
  if (status != ZX_OK) {
    LOG(ERROR,
        "Failed to select interface (id %u, alt %u, ep %u) "
        "when configuring format ndx %zu (status %d)\n",
        iid(), f.alt_id_, f.ep_addr_, ndx, status);
    return status;
  }

  // Do not attempt to set the sample rate if the endpoint supports
  // only one.  In theory, devices should ignore this request, but in
  // practice, some devices will refuse the command entirely, and we
  // will get ZX_ERR_IO_REFUSED back from the bus driver.
  //
  // Note: This method of determining whether or not an endpoint
  // supports only a single rate only works here because we currently
  // demand that all of our formats share a single endpoint address.
  // If this changes in the future, this heuristic will need to be
  // revisited.
  bool single_rate =
      (format_map_.size() == 1) && !(format_map_[0].range_.flags & ASF_RANGE_FLAG_FPS_CONTINUOUS);
  if (!single_rate) {
    // See section 5.2.3.2.3.1 of the USB Audio 1.0 spec.
    uint8_t buffer[3];
    buffer[0] = static_cast<uint8_t>(frames_per_second);
    buffer[1] = static_cast<uint8_t>(frames_per_second >> 8);
    buffer[2] = static_cast<uint8_t>(frames_per_second >> 16);
    status =
        usb_control_out(&parent_.usb_proto(), USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_ENDPOINT,
                        USB_AUDIO_SET_CUR, USB_AUDIO_SAMPLING_FREQ_CONTROL << 8, f.ep_addr_,
                        ZX_TIME_INFINITE, &buffer, sizeof(buffer));
    if (status != ZX_OK) {
      if (status == ZX_ERR_IO_REFUSED || status == ZX_ERR_IO_INVALID) {
        // clear the stall/error
        usb_reset_endpoint(&parent_.usb_proto(), f.ep_addr_);
      }

      LOG(ERROR, "Failed to set frame rate %u for ep address %u (status %d)\n", frames_per_second,
          f.ep_addr_, status);

      return status;
    }
  }

  return ZX_OK;
}

zx_status_t UsbAudioStreamInterface::ActivateIdleFormat() {
  if (idle_hdr_ == nullptr) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  ZX_DEBUG_ASSERT(idle_hdr_->bInterfaceNumber == iid());
  return usb_set_interface(&parent_.usb_proto(), iid(), idle_hdr_->bAlternateSetting);
}

void UsbAudioStreamInterface::LinkPath(std::unique_ptr<AudioPath> path) {
  ZX_DEBUG_ASSERT(path != nullptr);
  ZX_DEBUG_ASSERT(path_ == nullptr);
  ZX_DEBUG_ASSERT(direction() == path->direction());
  ZX_DEBUG_ASSERT(term_link() == path->stream_terminal().id());
  path_ = std::move(path);
}

zx_status_t UsbAudioStreamInterface::Format::Init(DescriptorListMemory::Iterator* iter) {
  ZX_DEBUG_ASSERT(iter != nullptr);
  ZX_DEBUG_ASSERT(iter->desc_list() == desc_list_);

  // Skip formats tags that we currently do not support or know how to deal
  // with.  Right now, we only deal with the linear PCM forms of Type I audio
  // formats.
  switch (class_hdr_->wFormatTag) {
    case USB_AUDIO_AS_FT_PCM:
    case USB_AUDIO_AS_FT_PCM8:
    case USB_AUDIO_AS_FT_IEEE_FLOAT:
      break;

    default:
      LOG(ERROR,
          "Unsupported format tag (0x%04hx) in class specific audio stream interface "
          "(iid %u, alt_id %u)\n",
          class_hdr_->wFormatTag, interface_hdr_->bInterfaceNumber, alt_id());
      return ZX_ERR_NOT_SUPPORTED;
  }

  // Next go looking for the other headers we will need in order to operate.
  // In specific, we need to find an audio format descriptor (specifically a
  // Type I descriptor), a general USB Endpoint descriptor, and a audio class
  // specific endpoint descriptor.
  //
  // If we encounter something which is not one of these things, then we have
  // run out of headers to parse.
  //
  // If we encounter duplicates of these descriptors, or we encounter
  // something clearly incompatible (such as a type II or type III format
  // descriptor), then we are confused and this interface should be ignored.
  // Be sure to skip headers like this if we return from the middle of the
  // do/while loop below.
  auto cleanup = fbl::MakeAutoCall([iter]() { iter->Next(); });
  do {
    auto hdr = iter->hdr();
    if (hdr == nullptr) {
      break;
    }

    if (hdr->bDescriptorType == USB_AUDIO_CS_INTERFACE) {
      // Stop parsing if this is not an audio format type descriptor
      auto ihdr = iter->hdr_as<usb_audio_desc_header>();
      if ((ihdr == nullptr) || (ihdr->bDescriptorSubtype != USB_AUDIO_AS_FORMAT_TYPE)) {
        break;
      }

      auto fmt_hdr = iter->hdr_as<usb_audio_as_format_type_hdr>();
      if (fmt_hdr == nullptr) {
        break;
      }

      if (fmt_hdr->bFormatType != USB_AUDIO_FORMAT_TYPE_I) {
        LOG(ERROR,
            "Unsupported format type (%u) in class specific audio stream format type "
            "interface (iid %u, alt_id %u)\n",
            fmt_hdr->bFormatType, interface_hdr_->bInterfaceNumber, alt_id());
        return ZX_ERR_NOT_SUPPORTED;
      }

      auto fmt_desc = iter->hdr_as<usb_audio_as_format_type_i_desc>();
      if ((fmt_desc_ != nullptr) || (fmt_desc == nullptr)) {
        LOG(ERROR,
            "Malformed or duplicate type 1 format type descriptor in class specific audio "
            "interface (iid %u, alt_id %u)\n",
            interface_hdr_->bInterfaceNumber, alt_id());
        return ZX_ERR_NOT_SUPPORTED;
      }

      // Stash the pointer, we'll sanity check a bit more once we are finished finding
      // headers.
      fmt_desc_ = fmt_desc;
    } else if (hdr->bDescriptorType == USB_DT_ENDPOINT) {
      auto ep_desc = iter->hdr_as<usb_endpoint_descriptor_t>();
      if (ep_desc == nullptr) {
        LOG(ERROR,
            "Malformed standard endpoint descriptor in class specific audio interface "
            "(iid %u, alt_id %u)\n",
            interface_hdr_->bInterfaceNumber, alt_id());
        return ZX_ERR_NOT_SUPPORTED;
      }

      // TODO(johngro): Come back and fix this.  There are devices with
      // multiple isochronous endpoints per format interface.  Device
      // which use an isochronous output endpoint with an Asynchronous
      // sync type seem to have an isochronous input endpoint as well
      // which is probably used for clock recovery.  Instead of
      // skipping/ignoring this endpoint, we really should be using it to
      // recover the device clock.
      if (ep_desc_ != nullptr) {
        LOG(WARN,
            "Skipping duplicate standard endpoint descriptor in class specific audio "
            "interface (iid %u, alt_id %u, ep_addr %u)\n",
            interface_hdr_->bInterfaceNumber, alt_id(), ep_desc->bEndpointAddress);
      } else {
        if ((usb_ep_type(ep_desc) != USB_ENDPOINT_ISOCHRONOUS) ||
            (usb_ep_sync_type(ep_desc) == USB_ENDPOINT_NO_SYNCHRONIZATION)) {
          LOG(WARN,
              "Skipping endpoint descriptor with unsupported attributes "
              "interface (iid %u, alt_id %u, ep_attr 0x%02x)\n",
              interface_hdr_->bInterfaceNumber, alt_id(), ep_desc->bmAttributes);
        } else {
          ep_desc_ = ep_desc;
        }
      }
    } else if (hdr->bDescriptorType == USB_AUDIO_CS_ENDPOINT) {
      // Stop parsing if this is not a class specific AS isochronous endpoint descriptor
      auto ihdr = iter->hdr_as<usb_audio_desc_header>();
      if ((ihdr == nullptr) || (ihdr->bDescriptorSubtype != USB_AUDIO_EP_GENERAL)) {
        break;
      }

      auto class_ep_desc = iter->hdr_as<usb_audio_as_isoch_ep_desc>();
      if (class_ep_desc == nullptr) {
        LOG(ERROR,
            "Malformed or class specific endpoint descriptor in class specific audio "
            "interface (iid %u, alt_id %u)\n",
            interface_hdr_->bInterfaceNumber, alt_id());
        return ZX_ERR_NOT_SUPPORTED;
      }

      if (class_ep_desc_ != nullptr) {
        LOG(WARN,
            "Skipping duplicate class specific endpoint descriptor in class specific "
            "audio interface (iid %u, alt_id %u\n",
            interface_hdr_->bInterfaceNumber, alt_id());
      } else {
        class_ep_desc_ = class_ep_desc;
      }
    } else {
      // We don't recognize this descriptor, so we have run out of
      // descriptors that we beleive belong to this format.  Move on to
      // sanity checks.
      break;
    }
  } while (iter->Next());
  cleanup.cancel();

  // Sanity check what we have found so far.  Right now, we need to have found...
  //
  // 1) A Type I audio format type descriptor (PCM)
  // 2) A standard Isochronous USB endpoint descriptor.
  // 3) An audio class specific endpoint descriptor.
  //
  // In addition, we need to make sure that the range of frame rates present
  // in the Type I descriptor makes sense.  If the range is continuous, the
  // array must contain *exactly* 2 entries.  If the range is discrete, then
  // the array must contain an integer number of entries, and must contain at
  // least one entry.
  if ((fmt_desc_ == nullptr) || (ep_desc_ == nullptr) || (class_ep_desc_ == nullptr)) {
    LOG(ERROR,
        "Missing one or more required descriptors in audio interface (iid %u, alt_id %u); "
        "Missing%s%s%s\n",
        interface_hdr_->bInterfaceNumber, alt_id(),
        (fmt_desc_ == nullptr) ? " [Type I Format Type Descriptor]" : "",
        (ep_desc_ == nullptr) ? " [Standard Endpoint Descriptor]" : "",
        (class_ep_desc_ == nullptr) ? " [Class Endpoint Descriptor]" : "");
    return ZX_ERR_NOT_SUPPORTED;
  }

  // hdr_as<> should have already verified this for us.
  ZX_DEBUG_ASSERT(fmt_desc_->bLength >= sizeof(*fmt_desc_));

  // Sanity check the size of the frame rate table.
  size_t expected_bytes =
      (frame_rate_cnt() ? frame_rate_cnt() : 2) * sizeof(usb_audio_as_samp_freq);
  size_t extra_bytes = fmt_desc_->bLength - sizeof(*fmt_desc_);
  if (expected_bytes != extra_bytes) {
    LOG(ERROR,
        "Bad frame rate table size in type 1 audio format type descriptor in audio interface "
        "(iid %u, alt_id %u).  Expected %zu, Got %zu\n",
        interface_hdr_->bInterfaceNumber, alt_id(), expected_bytes, extra_bytes);
    return ZX_ERR_INTERNAL;
  }

  // If this is a continuous range of frame rates, then the min/max order needs to be correct.
  if ((frame_rate_cnt() == 0) && (min_cont_frame_rate() > max_cont_frame_rate())) {
    LOG(ERROR,
        "Invalid continuous frame rate range [%u, %u] type 1 audio format type descriptor in "
        "audio interface (iid %u, alt_id %u).\n",
        min_cont_frame_rate(), max_cont_frame_rate(), interface_hdr_->bInterfaceNumber, alt_id());
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

}  // namespace usb
}  // namespace audio
