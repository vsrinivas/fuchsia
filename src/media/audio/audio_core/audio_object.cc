// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_object.h"

#include <trace/event.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/media/audio/audio_core/audio_device.h"
#include "src/media/audio/audio_core/audio_link.h"
#include "src/media/audio/audio_core/audio_renderer_impl.h"

namespace media::audio {

// static
fbl::RefPtr<AudioLink> AudioObject::LinkObjects(const std::shared_ptr<AudioObject>& source,
                                                const std::shared_ptr<AudioObject>& dest) {
  TRACE_DURATION("audio", "AudioObject::LinkObjects");
  // Assert this source is valid (AudioCapturers are disallowed).
  FX_DCHECK(source != nullptr);
  FX_DCHECK((source->type() == AudioObject::Type::AudioRenderer) ||
            (source->type() == AudioObject::Type::Output) ||
            (source->type() == AudioObject::Type::Input));

  // Assert this destination is valid (inputs and AudioRenderers disallowed).
  FX_DCHECK(dest != nullptr);
  FX_DCHECK((dest->type() == AudioObject::Type::Output) ||
            (dest->type() == AudioObject::Type::AudioCapturer));

  // Assert that we are not connecting looped-back-output to output.
  FX_DCHECK((source->type() != AudioObject::Type::Output) ||
            (dest->type() != AudioObject::Type::Output));

  // Create the link.
  fbl::RefPtr<AudioLink> link = fbl::MakeRefCounted<AudioLink>(source, dest);

  auto dest_init_result = source->InitializeDestLink(*dest);
  if (dest_init_result.is_error()) {
    return nullptr;
  }
  auto stream = dest_init_result.take_value();
  link->set_stream(stream);

  auto source_init_result = dest->InitializeSourceLink(*source, stream);
  if (source_init_result.is_error()) {
    return nullptr;
  }
  auto mixer = source_init_result.take_value();
  link->set_mixer(std::move(mixer));

  // Now lock both objects then add the link to the proper sets in both source and destination.
  {
    std::lock_guard<std::mutex> slock(source->links_lock_);
    std::lock_guard<std::mutex> dlock(dest->links_lock_);
    source->dest_links_.insert(link);
    dest->source_links_.insert(link);
  }

  source->OnLinkAdded();
  dest->OnLinkAdded();

  return link;
}

// static
void AudioObject::RemoveLink(const fbl::RefPtr<AudioLink>& link) {
  TRACE_DURATION("audio", "AudioObject::RemoveLink");
  FX_DCHECK(link != nullptr);

  link->Invalidate();

  auto& source = link->GetSource();
  auto& dest = link->GetDest();

  {
    std::lock_guard<std::mutex> slock(source.links_lock_);
    auto iter = source.dest_links_.find(link.get());
    if (iter != source.dest_links_.end()) {
      source.CleanupDestLink(dest);
      source.dest_links_.erase(iter);
    }
  }

  {
    std::lock_guard<std::mutex> dlock(dest.links_lock_);
    auto iter = dest.source_links_.find(link.get());
    if (iter != dest.source_links_.end()) {
      dest.CleanupSourceLink(source, link->stream());
      dest.source_links_.erase(iter);
    }
  }
}

// Call the provided function for each source link (passing the link as param). This distributes
// calls such as SetGain to every AudioCapturer path.
void AudioObject::ForEachSourceLink(const LinkFunction& source_task) {
  TRACE_DURATION("audio", "AudioObject::ForEachSourceLink");
  std::lock_guard<std::mutex> links_lock(links_lock_);

  // Callers (generally AudioCapturers) should never be linked to destinations.
  FX_DCHECK(dest_links_.is_empty());

  for (auto& link : source_links_) {
    source_task(link);
  }
}

// Call the provided function for each dest link (passing the link as a param). This distributes
// calls such as SetGain to every AudioRenderer output path.
void AudioObject::ForEachDestLink(const LinkFunction& dest_task) {
  TRACE_DURATION("audio", "AudioObject::ForEachDestLink");
  std::lock_guard<std::mutex> links_lock(links_lock_);

  // Callers (generally AudioRenderers) should never be linked to sources.
  FX_DCHECK(source_links_.is_empty());

  for (auto& link : dest_links_) {
    dest_task(link);
  }
}

// Call the provided function for each destination link, until one returns true.
bool AudioObject::ForAnyDestLink(const LinkBoolFunction& dest_task) {
  TRACE_DURATION("audio", "AudioObject::ForAnyDestLink");
  std::lock_guard<std::mutex> links_lock(links_lock_);

  for (auto& link : dest_links_) {
    if (dest_task(link)) {
      return true;  // This link satisfied the need; we are done.
    }
    // Else, continue inquiring with the remaining links.
  }
  return false;  // No link satisfied the need.
}

bool AudioObject::ForAnySourceLink(const LinkBoolFunction& source_task) {
  TRACE_DURATION("audio", "AudioObject::ForAnySourceLink");
  std::lock_guard<std::mutex> links_lock(links_lock_);

  for (auto& link : dest_links_) {
    if (source_task(link)) {
      return true;  // This link satisfied the need; we are done.
    }
    // Else, continue inquiring with the remaining links.
  }
  return false;  // No link satisfied the need.
}

void AudioObject::UnlinkSources() {
  TRACE_DURATION("audio", "AudioObject::UnlinkSources");
  typename AudioLink::Set<AudioLink::Source> old_links;
  {
    std::lock_guard<std::mutex> lock(links_lock_);
    old_links = std::move(source_links_);
  }
  UnlinkCleanup<AudioLink::Source>(&old_links);
}

void AudioObject::UnlinkDestinations() {
  TRACE_DURATION("audio", "AudioObject::UnlinkDestinations");
  typename AudioLink::Set<AudioLink::Dest> old_links;
  {
    std::lock_guard<std::mutex> lock(links_lock_);
    old_links = std::move(dest_links_);
  }
  UnlinkCleanup<AudioLink::Dest>(&old_links);
}

template <typename TagType>
void AudioObject::UnlinkCleanup(typename AudioLink::Set<TagType>* links) {
  TRACE_DURATION("audio", "AudioObject::UnlinkCleanup");
  FX_DCHECK(links != nullptr);

  // Note: we could just range-based for-loop over this set and call RemoveLink on each member.
  // Instead, we remove each element from our local set before calling RemoveLinks. This will make a
  // future transition to intrusive containers a bit easier. Explanations available on request.
  while (!links->is_empty()) {
    auto link = links->pop_front();
    RemoveLink(link);
    link = nullptr;
  }
}

}  // namespace media::audio
