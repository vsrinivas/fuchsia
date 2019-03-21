// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_object.h"

#include "src/lib/fxl/logging.h"
#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/audio_link_packet_source.h"
#include "src/media/audio/audio_core/audio_link_ring_buffer_source.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"

namespace media::audio {

// static
fbl::RefPtr<AudioLink> AudioObject::LinkObjects(
    const fbl::RefPtr<AudioObject>& source,
    const fbl::RefPtr<AudioObject>& dest) {
  // Assert this source is valid (AudioCapturers are disallowed).
  FXL_DCHECK(source != nullptr);
  FXL_DCHECK((source->type() == AudioObject::Type::AudioRenderer) ||
             (source->type() == AudioObject::Type::Output) ||
             (source->type() == AudioObject::Type::Input));

  // Assert this destination is valid (inputs and AudioRenderers disallowed).
  FXL_DCHECK(dest != nullptr);
  FXL_DCHECK((dest->type() == AudioObject::Type::Output) ||
             (dest->type() == AudioObject::Type::AudioCapturer));

  // Assert that we are not connecting looped-back-output to output.
  FXL_DCHECK((source->type() != AudioObject::Type::Output) ||
             (dest->type() != AudioObject::Type::Output));

  // Create a link of the appropriate type based on our source.
  fbl::RefPtr<AudioLink> link;
  if (source->type() == AudioObject::Type::AudioRenderer) {
    link = AudioLinkPacketSource::Create(
        fbl::RefPtr<AudioRendererImpl>::Downcast(source), dest);
  } else {
    link = AudioLinkRingBufferSource::Create(
        fbl::RefPtr<AudioDevice>::Downcast(source), dest);
  }

  // Give source and destination a chance to initialize (or reject) the link.
  zx_status_t res;
  res = source->InitializeDestLink(link);
  if (res != ZX_OK) {
    return nullptr;
  }
  res = dest->InitializeSourceLink(link);
  if (res != ZX_OK) {
    return nullptr;
  }

  // Now lock both objects, make sure both are still allowing new links,
  // then add the link to the proper sets in both source and destination.
  {
    fbl::AutoLock slock(&source->links_lock_);
    fbl::AutoLock dlock(&dest->links_lock_);
    if (source->new_links_allowed_ && dest->new_links_allowed_) {
      source->dest_links_.insert(link);
      dest->source_links_.insert(link);
    } else {
      link.reset();
    }
  }

  // TODO(johngro): if we must poke the destination, in case it needs to wake
  // and do specific work because of this new source, this where to do it.

  return link;
}

// static
void AudioObject::RemoveLink(const fbl::RefPtr<AudioLink>& link) {
  FXL_DCHECK(link != nullptr);

  link->Invalidate();

  const fbl::RefPtr<AudioObject>& source = link->GetSource();
  FXL_DCHECK(source != nullptr);
  {
    fbl::AutoLock slock(&source->links_lock_);
    auto iter = source->dest_links_.find(link.get());
    if (iter != source->dest_links_.end()) {
      source->dest_links_.erase(iter);
    }
  }

  const fbl::RefPtr<AudioObject>& dest = link->GetDest();
  FXL_DCHECK(dest != nullptr);
  {
    fbl::AutoLock dlock(&dest->links_lock_);
    auto iter = dest->source_links_.find(link.get());
    if (iter != dest->source_links_.end()) {
      dest->source_links_.erase(iter);
    }
  }
}

// Call the provided function for each source link (passing the link as param).
// This distributes calls such as SetGain to every AudioCapturer path.
void AudioObject::ForEachSourceLink(const LinkFunction& source_task) {
  fbl::AutoLock links_lock(&links_lock_);

  // Callers (generally AudioCapturers) should never be linked to destinations.
  FXL_DCHECK(dest_links_.is_empty());

  for (auto& link : source_links_) {
    source_task(link);
  }
}

// Call the provided function for each dest link (passing the link as a param).
// This distributes calls such as SetGain to every AudioRenderer output path.
void AudioObject::ForEachDestLink(const LinkFunction& dest_task) {
  fbl::AutoLock links_lock(&links_lock_);

  // Callers (generally AudioRenderers) should never be linked to sources.
  FXL_DCHECK(source_links_.is_empty());

  for (auto& link : dest_links_) {
    dest_task(link);
  }
}

// Call the provided function for each destination link, until one returns true.
bool AudioObject::ForAnyDestLink(const LinkBoolFunction& dest_task) {
  fbl::AutoLock links_lock(&links_lock_);

  FXL_DCHECK(source_links_.is_empty());

  for (auto& link : dest_links_) {
    if (dest_task(link)) {
      return true;  // This link satisfied the need; we are done.
    }
    // Else, continue inquiring with the remaining links.
  }
  return false;  // No link satisfied the need.
}

void AudioObject::UnlinkSources() {
  typename AudioLink::Set<AudioLink::Source> old_links;
  {
    fbl::AutoLock lock(&links_lock_);
    old_links = std::move(source_links_);
  }
  UnlinkCleanup<AudioLink::Source>(&old_links);
}

void AudioObject::UnlinkDestinations() {
  typename AudioLink::Set<AudioLink::Dest> old_links;
  {
    fbl::AutoLock lock(&links_lock_);
    old_links = std::move(dest_links_);
  }
  UnlinkCleanup<AudioLink::Dest>(&old_links);
}

zx_status_t AudioObject::InitializeSourceLink(const fbl::RefPtr<AudioLink>&) {
  return ZX_OK;
}

zx_status_t AudioObject::InitializeDestLink(const fbl::RefPtr<AudioLink>&) {
  return ZX_OK;
}

template <typename TagType>
void AudioObject::UnlinkCleanup(typename AudioLink::Set<TagType>* links) {
  FXL_DCHECK(links != nullptr);

  // Note: we could just range-based for-loop over this set and call RemoveLink
  // on each member. Instead, we remove each element from our local set before
  // calling RemoveLinks. This will make a future transition to using intrusive
  // containers a bit easier. Explanations available on request.
  while (!links->is_empty()) {
    auto link = links->pop_front();
    RemoveLink(link);
    link.reset();
  }
}

}  // namespace media::audio
