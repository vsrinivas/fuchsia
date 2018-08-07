// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_object.h"

#include "garnet/bin/media/audio_core/audio_device.h"
#include "garnet/bin/media/audio_core/audio_link.h"
#include "garnet/bin/media/audio_core/audio_link_packet_source.h"
#include "garnet/bin/media/audio_core/audio_link_ring_buffer_source.h"
#include "garnet/bin/media/audio_core/audio_out_impl.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {

// static
std::shared_ptr<AudioLink> AudioObject::LinkObjects(
    const fbl::RefPtr<AudioObject>& source,
    const fbl::RefPtr<AudioObject>& dest) {
  // Assert that this is a valid source (capturers may not be sources)
  FXL_DCHECK(source != nullptr);
  FXL_DCHECK((source->type() == AudioObject::Type::Renderer) ||
             (source->type() == AudioObject::Type::Output) ||
             (source->type() == AudioObject::Type::Input));

  // Assert that this is a valid destination (inputs and renderers may not be
  // destinations)
  FXL_DCHECK(dest != nullptr);
  FXL_DCHECK((dest->type() == AudioObject::Type::Output) ||
             (dest->type() == AudioObject::Type::Capturer));

  // Assert that we are not trying to connect an output to an output.
  FXL_DCHECK((source->type() != AudioObject::Type::Output) ||
             (dest->type() != AudioObject::Type::Output));

  // Create a link of the appropriate type based on our source.
  std::shared_ptr<AudioLink> link;
  if (source->type() == AudioObject::Type::Renderer) {
    link = AudioLinkPacketSource::Create(
        fbl::RefPtr<AudioOutImpl>::Downcast(source), dest);
  } else {
    link = AudioLinkRingBufferSource::Create(
        fbl::RefPtr<AudioDevice>::Downcast(source), dest);
  }

  // Give the the source and the destination their chances to initialize (or
  // reject) the link.
  zx_status_t res;
  res = source->InitializeDestLink(link);
  if (res != ZX_OK) {
    return nullptr;
  }
  res = dest->InitializeSourceLink(link);
  if (res != ZX_OK) {
    return nullptr;
  }

  // Now lock both objects, make sure that both are still allowing new links,
  // then add the link to the proper sets in both the source and the
  // destination.
  {
    fbl::AutoLock slock(&source->links_lock_);
    fbl::AutoLock dlock(&dest->links_lock_);
    if (source->new_links_allowed_ && dest->new_links_allowed_) {
      __UNUSED auto sres = source->dest_links_.emplace(link);
      __UNUSED auto dres = dest->source_links_.emplace(link);
      FXL_DCHECK(sres.second);
      FXL_DCHECK(dres.second);
    } else {
      link.reset();
    }
  }

  // TODO(johngro): if we need to poke the destination to let it know that it
  // might need to wake up and do some work because it has a new source to
  // handle, this would be the place to do so.

  return link;
}

// static
void AudioObject::RemoveLink(const AudioLinkPtr& link) {
  FXL_DCHECK(link != nullptr);

  link->Invalidate();

  const fbl::RefPtr<AudioObject>& source = link->GetSource();
  FXL_DCHECK(source != nullptr);
  {
    fbl::AutoLock slock(&source->links_lock_);
    auto iter = source->dest_links_.find(link);
    if (iter != source->dest_links_.end()) {
      source->dest_links_.erase(iter);
    }
  }

  const fbl::RefPtr<AudioObject>& dest = link->GetDest();
  FXL_DCHECK(dest != nullptr);
  {
    fbl::AutoLock dlock(&dest->links_lock_);
    auto iter = dest->source_links_.find(link);
    if (iter != dest->source_links_.end()) {
      dest->source_links_.erase(iter);
    }
  }
}

void AudioObject::UnlinkSources() {
  AudioLinkSet old_links;
  {
    fbl::AutoLock lock(&links_lock_);
    old_links = std::move(source_links_);
  }
  UnlinkCleanup(&old_links);
}

void AudioObject::UnlinkDestinations() {
  AudioLinkSet old_links;
  {
    fbl::AutoLock lock(&links_lock_);
    old_links = std::move(dest_links_);
  }
  UnlinkCleanup(&old_links);
}

zx_status_t AudioObject::InitializeSourceLink(const AudioLinkPtr& link) {
  return ZX_OK;
}

zx_status_t AudioObject::InitializeDestLink(const AudioLinkPtr& link) {
  return ZX_OK;
}

void AudioObject::UnlinkCleanup(AudioLinkSet* links) {
  FXL_DCHECK(links != nullptr);

  // Note: we could just range-based for-loop over this set and call RemoveLink
  // on each member.  Instead, we remove each element from our local set before
  // calling RemoveLinks.  This is to make the transition to using intrusive
  // containers (at a future date) a bit easier.  Explainations available upon
  // request.
  while (!links->empty()) {
    auto link = std::move(*links->begin());
    links->erase(links->begin());
    RemoveLink(link);
  }
}

}  // namespace audio
}  // namespace media
