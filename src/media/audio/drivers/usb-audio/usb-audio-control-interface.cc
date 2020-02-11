// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-audio-control-interface.h"

#include <memory>
#include <utility>

#include <fbl/auto_call.h>

#include "debug-logging.h"
#include "usb-audio-device.h"
#include "usb-audio-units.h"

namespace audio {
namespace usb {

// We use our parent's log prefix
const char* UsbAudioControlInterface::log_prefix() const { return parent_.log_prefix(); }

UsbAudioControlInterface::UsbAudioControlInterface(UsbAudioDevice* parent) : parent_(*parent) {
  ZX_DEBUG_ASSERT(parent != nullptr);
}

UsbAudioControlInterface::~UsbAudioControlInterface() {}

std::unique_ptr<UsbAudioControlInterface> UsbAudioControlInterface::Create(UsbAudioDevice* parent) {
  if (parent == nullptr) {
    GLOBAL_LOG(ERROR, "null parent passed to %s\n", __PRETTY_FUNCTION__);
    return nullptr;
  }

  fbl::AllocChecker ac;
  std::unique_ptr<UsbAudioControlInterface> ret(new (&ac) UsbAudioControlInterface(parent));
  if (ac.check()) {
    return ret;
  }

  return nullptr;
}

zx_status_t UsbAudioControlInterface::Initialize(DescriptorListMemory::Iterator* iter) {
  ZX_DEBUG_ASSERT(iter != nullptr);
  ZX_DEBUG_ASSERT(iter->desc_list() != nullptr);

  // It is an error to attempt to initialize this class twice.
  if (desc_list_ != nullptr) {
    LOG(ERROR, "Attempted to initialize control interface twice\n");
    return ZX_ERR_BAD_STATE;
  }

  desc_list_ = iter->desc_list();
  interface_hdr_ = iter->hdr_as<usb_interface_descriptor_t>();

  // These should already have been checked before Initialize was called.
  ZX_DEBUG_ASSERT(interface_hdr_ != nullptr);
  ZX_DEBUG_ASSERT(interface_hdr_->bInterfaceClass == USB_CLASS_AUDIO);
  ZX_DEBUG_ASSERT(interface_hdr_->bInterfaceSubClass == USB_SUBCLASS_AUDIO_CONTROL);

  // Parse all of the descriptors which belong to this audio control
  // interface.  As soon as we find something which does not belong to the
  // interface, break out of the parse loop, leaving the iterator pointing at
  // the next descriptor (if any).  Then try to make sense of the descriptors
  // we did find.
  while (iter->Next()) {
    {
      auto hdr = iter->hdr();
      if (!hdr || (hdr->bDescriptorType != USB_AUDIO_CS_INTERFACE)) {
        break;
      }
    }

    auto hdr = iter->hdr_as<usb_audio_desc_header>();
    if (!hdr) {
      LOG(WARN, "Badly formed audio control descriptor header @ offset %zu\n", iter->offset());
      continue;
    }

    if (hdr->bDescriptorSubtype == USB_AUDIO_AC_HEADER) {
      if (class_hdr_ == nullptr) {
        class_hdr_ = iter->hdr_as<usb_audio_ac_header_desc>();
        if (class_hdr_ == nullptr) {
          LOG(WARN, "Badly formed audio control class specific header @ offset %zu\n",
              iter->offset());
        }
      } else {
        LOG(WARN, "Duplicate audio control class specific header @ offset %zu\n", iter->offset());
      }

      continue;
    }

    auto unit = AudioUnit::Create(*iter, interface_hdr_->bInterfaceNumber);
    if (unit == nullptr) {
      LOG(WARN, "Failed to create audio Terminal/Unit (type %u) @ offset %zu\n",
          hdr->bDescriptorSubtype, iter->offset());
    } else {
      // Add our new unit to the collection we are building up.  There
      // should be no collision; all unit IDs are supposed to be
      // unique within a given control interface.  If we encounter a
      // collision, log a warning and move on (eg, just try to do the
      // best we can).
      uint32_t id = unit->id();
      if (!units_.insert_or_find(std::move(unit))) {
        LOG(WARN, "Collision when attempting to add unit id %u; skipping this unit\n", id);
      }
    }
  }

  // Next, give each Unit/Terminal a chance to probe any state they will need
  // to operate which will require performing actual USB transactions.
  for (auto& unit : units_) {
    zx_status_t res = unit.Probe(parent_.usb_proto());
    if (res != ZX_OK) {
      LOG(ERROR, "Failed to probe %s (id %u) during initialization!\n", unit.type_name(),
          unit.id());
      return res;
    }
  }

  // OK - now that we have our set of descriptors, attempt to find the audio
  // paths through this graph that we intend to publish.  The algorithm used
  // here is not particularly sophisticated.  Basically, we are going to start
  // at each output terminal in the set and attempt to trace our way back to
  // an input terminal that forms a path from host to pin (or vice versa).
  // Pin-to-pin or host-to-host paths are ignored, although if we someday want
  // to recognize sidetone paths, we should probably pay some attention to the
  // pin to pin paths.
  //
  // We explore the graph using a depth first recursive search using a state
  // bit stored in the terminal/unit classes to avoid cycles.  Since the unit
  // IDs used by terminal/units are 8-bits, we can only recurse an absolute
  // maximum of 256 times, which should be safe from stack overflow for the
  // class of hardware this driver is intended for.
  //
  // Once any valid path from output to input has been found, we stop the
  // search, even if there may be another path to consider.  For most simple
  // devices out there, this should be sufficient, however as time goes on we
  // may discover more complicated devices that will require us to revisit
  // this algorithm and make it a bit smarter.  Failing that, a custom driver
  // would be needed for these more complicated hypothetical devices.
  for (auto iter = units_.begin(); iter.IsValid(); ++iter) {
    // We are only interested in output terminals.  Skip all of the rest.
    if (iter->type() != AudioUnit::Type::OutputTerminal) {
      continue;
    }

    // Do the search.  If it succeed, we will get a reference to an
    // AudioPath object back.
    LOG(TRACE, "Beginning trace for Output Terminal id %u\n", iter->id());
    auto path = TracePath(static_cast<const OutputTerminal&>(*iter), iter);
    if (path != nullptr) {
      LOG(TRACE, "Found valid path!\n");

      zx_status_t status = path->Setup(parent_.usb_proto());
      if (status != ZX_OK) {
        LOG(TRACE, "Failed to setup path! (status %d)\n", status);
      } else {
        paths_.push_back(std::move(path));
      }
    } else {
      LOG(TRACE, "No valid path found\n");
    }
  }

  // Now that we have found all of our valid paths, go over our list of
  // discovered units and mute any volume controls in feature units which are
  // not currently being used by any audio paths.
  for (auto& unit : units_) {
    if ((unit.type() == AudioUnit::Type::FeatureUnit) && !unit.in_use()) {
      auto& feature_unit = static_cast<FeatureUnit&>(unit);
      feature_unit.SetMute(parent_.usb_proto(), true);
    }

    // TODO(johngro) : If we encounter un-used mixer nodes, we should set
    // all of their inputs to maximum dB down in an attempt to effectively
    // mute them.
  }

  return ZX_OK;
}

std::unique_ptr<AudioPath> UsbAudioControlInterface::TracePath(const OutputTerminal& out_term,
                                                               const UnitMap::iterator& current,
                                                               uint32_t level) {
  // Flag the current node as having been visited and setup a cleanup task to
  // clear the flag as we unwind.
  auto cleanup = fbl::MakeAutoCall([&current]() { current->visited() = false; });
  ZX_DEBUG_ASSERT(!current->visited());
  current->visited() = true;
  LOG(TRACE, "Visiting unit id %u, type %s\n", current->id(), current->type_name());

  // If we have reached an input terminal, then check to see if it is of the
  // proper type.  If so, create a new path object and start to unwind the
  // stack, stashing the references to the unit which define the path in the
  // process.  Otherwise, this is a dead end.  Just return null and keep
  // looking.
  if (current->type() == AudioUnit::Type::InputTerminal) {
    // We have found a valid path of one of these terminals is a USB stream
    // terminal, while the other terminal is anything which is not a USB
    // terminal (stream or otherwise).
    const auto& in_term = static_cast<const InputTerminal&>(*current);
    if ((out_term.is_stream_terminal() && !in_term.is_usb_terminal()) ||
        (!out_term.is_stream_terminal() && in_term.is_usb_terminal())) {
      auto ret = AudioPath::Create(level + 1);
      if (ret != nullptr) {
        ret->AddUnit(level, current.CopyPointer());
      }
      return ret;
    }

    LOG(TRACE, "Skipping incompatible input terminal (in type 0x%04hx, out type 0x%04hx)\n",
        in_term.terminal_type(), out_term.terminal_type());
    return nullptr;
  }

  for (uint32_t i = 0; i < current->source_count(); ++i) {
    uint32_t source_id = current->source_id(i);
    auto next = units_.find(source_id);
    if (!next.IsValid()) {
      LOG(WARN, "Can't find upstream unit id %u while tracing from unit id %u.\n", source_id,
          current->id());
      continue;
    }

    if (next->visited()) {
      LOG(TRACE, "Skipping already visited unit id %u while tracing from unit id %u\n", source_id,
          current->id());
      continue;
    }

    // Recurse down this path.  If it finds a valid path, stash ourselves in
    // the path and unwind.
    auto path = TracePath(out_term, next, level + 1);
    if (path != nullptr) {
      path->AddUnit(level, current.CopyPointer());
      return path;
    }
  }

  return nullptr;
}

}  // namespace usb
}  // namespace audio
