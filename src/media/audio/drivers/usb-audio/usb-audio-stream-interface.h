// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_STREAM_INTERFACE_H_
#define SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_STREAM_INTERFACE_H_

#include <zircon/device/audio.h>
#include <zircon/hw/usb/audio.h>

#include <memory>
#include <utility>

#include <fbl/intrusive_double_list.h>
#include <fbl/vector.h>

#include "usb-audio-descriptors.h"
#include "usb-audio.h"

namespace audio {
namespace usb {

class UsbAudioDevice;
class AudioPath;

class UsbAudioStreamInterface
    : public fbl::DoublyLinkedListable<std::unique_ptr<UsbAudioStreamInterface>> {
 public:
  // A small helper struct which maps from a Fuchsia format range to the
  // alternate interface ID which supports that range.
  struct FormatMapEntry {
    const audio_stream_format_range_t range_;

    // The alternate interface ID, endpoint address, and maximum request
    // size  which need to be used when configuring the stream interface to
    // use the format described by range_.
    const uint8_t alt_id_;
    const uint8_t ep_addr_;
    const uint16_t max_req_size_;

    FormatMapEntry(const audio_stream_format_range_t& range, uint8_t alt_id, uint8_t ep_addr,
                   uint16_t max_req_size)
        : range_(range), alt_id_(alt_id), ep_addr_(ep_addr), max_req_size_(max_req_size) {}
  };

  // Note that UsbAudioStreamInterfaces are entirely owned by UsbAudioDevice
  // instances.  The stream interface needs to hold a pointer to its parent,
  // so it is critically important that the owning parent is certain that
  // the stream interface (and all of its children) have been properly shut
  // down before exiting.  At all times, the lifetime of the stream interface
  // needs to be a subset of the lifetime of the device parent.
  //
  // Note, the iterator passed to the create method *must* be pointing at a
  // valid interface header with class == audio and subclass == streaming
  // interface.  The interface ID encountered in this first header will become
  // the interface ID of this StreamInterface object.
  static std::unique_ptr<UsbAudioStreamInterface> Create(UsbAudioDevice* parent,
                                                         DescriptorListMemory::Iterator* iter);

  // Called to add a new alternate streaming interface to this StreamInterface
  // object.  The iterator must be pointing at a valid audio stream interface
  // descriptor which shares a an IID with this object.
  zx_status_t AddInterface(DescriptorListMemory::Iterator* iter);

  // Called after all of the interface descriptors have been discovered and
  // added to this stream interface to allow the stream interface a chance to
  // build its list of format ranges and the alternate interface ID which
  // support them.
  zx_status_t BuildFormatMap();

  // Called from the UsbAudioStream to lookup the index of a format which
  // matches the user's request.  Note, this does not actually cause the
  // interface to switch to this format.  Use ActivateFormat, passing the
  // index retrieved from there, to achieve that.
  zx_status_t LookupFormat(uint32_t frames_per_second, uint16_t channels,
                           audio_sample_format_t sample_format, size_t* out_format_ndx);

  // Called from the UsbAudioStream to activate the chosen format interface
  // and to configure the specific frame rate for that interface.
  zx_status_t ActivateFormat(size_t ndx, uint32_t frames_per_second);

  // Called from the UsbAudioStream to activate the alternate idle interface
  // (if any).  Will return ZX_ERR_NOT_SUPPORTED if there is no idle
  // interface.
  zx_status_t ActivateIdleFormat();

  // Called at the end of device probing to link a discovered audio path to
  // this stream interface.
  void LinkPath(std::unique_ptr<AudioPath> path);

  uint8_t iid() const { return iid_; }
  uint16_t max_req_size() const { return max_req_size_; }
  const std::unique_ptr<AudioPath>& path() { return path_; }
  const fbl::Vector<FormatMapEntry>& formats() { return format_map_; }

  // Properties shared by all formats of this stream interface.
  uint8_t term_link() const { return term_link_; }
  uint8_t ep_addr() const { return ep_addr_; }
  uint8_t ep_attr() const { return ep_attr_; }

  Direction direction() const {
    return (ep_addr() & USB_ENDPOINT_DIR_MASK) ? Direction::Input : Direction::Output;
  }

  EndpointSyncType ep_sync_type() const {
    return static_cast<EndpointSyncType>(ep_attr() & USB_ENDPOINT_SYNCHRONIZATION_MASK);
  }

  // Accessor for debug logging.
  const char* log_prefix() const;

 private:
  friend class std::default_delete<UsbAudioStreamInterface>;

  // An internal helper class which contains all of the information we need to
  // support an alternate interface setting which supports a given format.
  class Format : public fbl::DoublyLinkedListable<std::unique_ptr<Format>> {
   public:
    Format(const UsbAudioStreamInterface* parent, fbl::RefPtr<DescriptorListMemory> desc_list,
           const usb_interface_descriptor_t* interface_hdr,
           const usb_audio_as_header_desc* class_hdr)
        : parent_(parent),
          desc_list_(std::move(desc_list)),
          interface_hdr_(interface_hdr),
          class_hdr_(class_hdr) {}

    // clang-format off
        const char* log_prefix()     const { return parent_->log_prefix(); }
        uint8_t     iid()            const { return interface_hdr_->bInterfaceNumber; }
        uint8_t     alt_id()         const { return interface_hdr_->bAlternateSetting; }
        uint8_t     term_link()      const { return class_hdr_->bTerminalLink; }
        uint16_t    format_tag()     const { return class_hdr_->wFormatTag; }
        uint8_t     ep_addr()        const { return ep_desc_->bEndpointAddress; }
        uint8_t     ep_attr()        const { return ep_desc_->bmAttributes; }
        uint16_t    max_req_size()   const { return ep_desc_->wMaxPacketSize; }
        uint8_t     frame_rate_cnt() const { return fmt_desc_->bSamFreqType; }
        uint8_t     ch_count()       const { return fmt_desc_->bNrChannels; }
        uint8_t     bit_resolution() const { return fmt_desc_->bBitResolution; }
        uint8_t     subframe_bytes() const { return fmt_desc_->bSubFrameSize; }
    // clang-format on

    // Min/Max continuous frame rates.  Valid *only* after initialize has
    // been successfully called, and *only* if frame_rate_cnt() == 0.
    uint32_t min_cont_frame_rate() const {
      ZX_DEBUG_ASSERT(frame_rate_cnt() == 0);
      return UnpackFrameRate(fmt_desc_->tSamFreq[0]);
    }

    uint32_t max_cont_frame_rate() const {
      ZX_DEBUG_ASSERT(frame_rate_cnt() == 0);
      return UnpackFrameRate(fmt_desc_->tSamFreq[1]);
    }

    // Fetch discrete frame rate #ndx.  Valid *only* after initialize has
    // been successfully called, and *only* if ndx < frame_rate_cnt()
    uint32_t frame_rate(uint8_t ndx) const {
      ZX_DEBUG_ASSERT(ndx < frame_rate_cnt());
      return UnpackFrameRate(fmt_desc_->tSamFreq[ndx]);
    }

    zx_status_t Init(DescriptorListMemory::Iterator* iter);

   private:
    friend class std::default_delete<Format>;
    ~Format() = default;

    // Packing format described in section 2.2.5 of USB Device Class
    // Definition for Audio Data Formats.
    static inline uint32_t UnpackFrameRate(const usb_audio_as_samp_freq& rate) {
      return (static_cast<uint32_t>(rate.freq[2]) << 16) |
             (static_cast<uint32_t>(rate.freq[1]) << 8) | static_cast<uint32_t>(rate.freq[0]);
    }

    // Determined at construction time
    const UsbAudioStreamInterface* parent_;
    const fbl::RefPtr<DescriptorListMemory> desc_list_;
    const usb_interface_descriptor_t* const interface_hdr_;
    const usb_audio_as_header_desc* const class_hdr_;

    // Determined at initialization time
    const usb_audio_as_format_type_i_desc* fmt_desc_ = nullptr;
    const usb_endpoint_descriptor_t* ep_desc_ = nullptr;
    const usb_audio_as_isoch_ep_desc* class_ep_desc_ = nullptr;
  };

  UsbAudioStreamInterface(UsbAudioDevice* parent, fbl::RefPtr<DescriptorListMemory> desc_list,
                          uint8_t iid)
      : parent_(*parent), iid_(iid), desc_list_(std::move(desc_list)) {
    ZX_DEBUG_ASSERT(parent != nullptr);
  }
  ~UsbAudioStreamInterface() = default;

  // The reference to our parent.  Note, because of this unmanaged reference,
  // it is critically important that the surrounding code ensure that we never
  // outlive our parent device.
  UsbAudioDevice& parent_;

  // The unique interface ID for this group of alternate interface descriptions.
  const uint8_t iid_;

  // Cached, unmanaged pointers to our interface and class descriptors.  The
  // memory which backs these descriptors is kept alive by the top level
  // desc_list_ reference.
  //
  // TODO(johngro) : this desc_list_ memory is contained in our parent
  // UsbAudioDevice.  Since we have already committed to having a lifetime
  // which is strictly <= the lifetime of our parent, we should probably just
  // access the descriptor memory using our parent instead of holding our own
  // reference to it.
  const fbl::RefPtr<DescriptorListMemory> desc_list_;

  // A pointer to an "idle" interface; IOW an interface which defines no
  // endpoints.  While not all audio streaming interfaces have one of these,
  // many seem to.  In theory, this allows a stream interface to save
  // isochronos bandwidth by selecting an alternate interface which requires
  // no isoch bandwidth allocation when the device is idle.
  const usb_interface_descriptor_t* idle_hdr_ = nullptr;

  // The terminal link ID which is shared by all of the valid formats we have
  // discovered.
  uint8_t term_link_ = 0xFF;

  // The endpoint address and attributes which are shared by all of the valid
  // formats we have discovered.
  uint8_t ep_addr_ = 0xFF;
  uint8_t ep_attr_ = 0x0;

  // The largest maximum request size computed across all of our discovered
  // endpoints.
  uint16_t max_req_size_ = 0;

  // A list of the formats (generic descriptors followed by a class specific
  // interface descriptor) we have discovered.
  fbl::DoublyLinkedList<std::unique_ptr<Format>> formats_;

  // The path through the control interface's terminal/unit graph that this
  // streaming interface is linked to.
  std::unique_ptr<AudioPath> path_;

  // A vector which contains the mappings from Fuchsia format ranges to the
  // alternate interface ID of the interface which supports that format range.
  fbl::Vector<FormatMapEntry> format_map_;
};

}  // namespace usb
}  // namespace audio

#endif  // SRC_MEDIA_AUDIO_DRIVERS_USB_AUDIO_USB_AUDIO_STREAM_INTERFACE_H_
