// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_AUDIO_USB_AUDIO_USB_AUDIO_CONTROL_INTERFACE_H_
#define ZIRCON_SYSTEM_DEV_AUDIO_USB_AUDIO_USB_AUDIO_CONTROL_INTERFACE_H_

#include <zircon/hw/usb/audio.h>

#include <memory>

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>

#include "usb-audio-descriptors.h"
#include "usb-audio-path.h"
#include "usb-audio-units.h"

namespace audio {
namespace usb {

class UsbAudioDevice;

class UsbAudioControlInterface {
 public:
  // Note that UsbAudioControlInterfaces are entirely owned by UsbAudioDevice
  // instances.  The control interface needs to hold a pointer to its parent,
  // so it is critically important that the owning parent is certain that the
  // control interface (and all of its children) have been properly shut down
  // before exiting.  At all times, the lifetime of the control interface
  // needs to be a subset of the lifetime of the device parent.
  static std::unique_ptr<UsbAudioControlInterface> Create(UsbAudioDevice* parent);
  zx_status_t Initialize(DescriptorListMemory::Iterator* iter);

  const char* log_prefix() const;

  // Extract the AudioPath whose streaming terminal interface link ID matches
  // the users request, if any.  Otherwise, simply return nullptr.
  std::unique_ptr<AudioPath> ExtractPath(uint8_t term_link, Direction direction) {
    return paths_.erase_if([term_link, direction](const AudioPath& path) -> bool {
      return (path.stream_terminal().id() == term_link) && (path.direction() == direction);
    });
  }

 private:
  friend class std::default_delete<UsbAudioControlInterface>;
  using UnitMap = fbl::WAVLTree<uint32_t, fbl::RefPtr<AudioUnit>>;

  UsbAudioControlInterface(UsbAudioDevice* parent);
  ~UsbAudioControlInterface();

  // A recursive method used to find interesting audio paths in the
  // unit/terminal graph.  See the comment at the end of the Init
  // implementation for details about the algorithm used to find these paths.
  std::unique_ptr<AudioPath> TracePath(const OutputTerminal& out_term,
                                       const UnitMap::iterator& current, uint32_t level = 0);

  // The reference to our parent.  Note, because of this unmanaged reference,
  // it is critically important that the surrounding code ensure that we never
  // outlive our parent device.
  UsbAudioDevice& parent_;

  // Cached, unmanaged pointers to our interface and class descriptors.  The
  // memory which backs these descriptors is kept alive by the top level
  // desc_list_ reference.
  //
  // TODO(johngro) : this desc_list_ memory is contained in our parent
  // UsbAudioDevice.  Since we have already committed to having a lifetime
  // which is strictly <= the lifetime of our parent, we should probably just
  // access the descriptor memory using our parent instead of holding our own
  // reference to it.
  fbl::RefPtr<DescriptorListMemory> desc_list_;
  const usb_interface_descriptor_t* interface_hdr_ = nullptr;
  const usb_audio_ac_header_desc* class_hdr_ = nullptr;

  // All unit/terminal IDs for a given Audio Control Interface
  //
  // TODO(johngro): Strictly speaking, we don't really need to hold onto this.
  // If we wanted, we could just keep this as a local variable used during
  // Init and discard it afterwards.
  UnitMap units_;

  // The complete set of valid paths we have discovered.
  fbl::DoublyLinkedList<std::unique_ptr<AudioPath>> paths_;
};

}  // namespace usb
}  // namespace audio

#endif  // ZIRCON_SYSTEM_DEV_AUDIO_USB_AUDIO_USB_AUDIO_CONTROL_INTERFACE_H_
