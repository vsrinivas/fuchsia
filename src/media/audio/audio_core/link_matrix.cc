// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/link_matrix.h"

namespace media::audio {
namespace {

using LinkType = std::pair<AudioObject::Type, AudioObject::Type>;
constexpr std::array<LinkType, 3> kValidLinks{
    LinkType{AudioObject::Type::AudioRenderer, AudioObject::Type::Output},
    LinkType{AudioObject::Type::Input, AudioObject::Type::AudioCapturer},
    LinkType{AudioObject::Type::Output, AudioObject::Type::AudioCapturer}};

void CheckLinkIsValid(AudioObject* source, AudioObject* dest) {
  FX_CHECK(source != nullptr);
  FX_CHECK(dest != nullptr);

  FX_CHECK(std::any_of(kValidLinks.begin(), kValidLinks.end(),
                       [source_type = source->type(), dest_type = dest->type()](auto pair) {
                         auto [valid_source_type, valid_dest_type] = pair;
                         return source_type == valid_source_type && dest_type == valid_dest_type;
                       }));
}

}  // namespace

zx_status_t LinkMatrix::LinkObjects(std::shared_ptr<AudioObject> source,
                                    std::shared_ptr<AudioObject> dest,
                                    std::shared_ptr<const LoudnessTransform> loudness_transform)
    FXL_LOCKS_EXCLUDED(lock_) {
  CheckLinkIsValid(source.get(), dest.get());

  auto dest_link_init_result = source->InitializeDestLink(*dest);
  if (dest_link_init_result.is_error()) {
    return dest_link_init_result.error();
  }
  auto stream = dest_link_init_result.take_value();

  auto source_link_init_result = dest->InitializeSourceLink(*source, stream);
  if (source_link_init_result.is_error()) {
    return source_link_init_result.error();
  }
  auto mixer = source_link_init_result.take_value();

  {
    std::lock_guard<std::mutex> lock(lock_);
    DestLinkSet(source.get()).insert(Link(dest, loudness_transform, stream, mixer));
    SourceLinkSet(dest.get()).insert(Link(source, loudness_transform, stream, mixer));
  }

  source->OnLinkAdded();
  dest->OnLinkAdded();

  return ZX_OK;
}

void LinkMatrix::Unlink(const AudioObject& key) FXL_LOCKS_EXCLUDED(lock_) {
  std::lock_guard<std::mutex> lock(lock_);

  auto dest_list = DestLinkSet(&key);
  std::for_each(dest_list.begin(), dest_list.end(), [this, &key](auto& dest) {
    auto& sources = SourceLinkSet(dest.key);
    auto source = sources.find(Link(&key));
    if (source == sources.end()) {
      return;
    }

    auto dest_object = dest.object.lock();
    if (dest_object) {
      dest_object->CleanupSourceLink(key, source->stream);
    }

    sources.erase(Link(&key));
  });

  auto source_list = SourceLinkSet(&key);
  std::for_each(source_list.begin(), source_list.end(), [this, &key](auto& source) {
    auto& dests = DestLinkSet(source.key);
    auto dest = dests.find(Link(&key));
    if (dest == dests.end()) {
      return;
    }

    auto source_object = source.object.lock();
    if (source_object) {
      source_object->CleanupDestLink(key);
    }

    dests.erase(Link(&key));
  });

  sources_.erase(&key);
  dests_.erase(&key);
}

void LinkMatrix::ForEachDestLink(const AudioObject& object, fit::function<void(LinkHandle)> f)
    FXL_LOCKS_EXCLUDED(lock_) {
  std::lock_guard<std::mutex> lock(lock_);

  for (auto& link : DestLinkSet(&object)) {
    if (auto ptr = link.object.lock()) {
      f(LinkHandle{.object = ptr,
                   .loudness_transform = link.loudness_transform,
                   .stream = link.stream,
                   .mixer = link.mixer});
    }
  }
}

void LinkMatrix::ForEachSourceLink(const AudioObject& object, fit::function<void(LinkHandle)> f)
    FXL_LOCKS_EXCLUDED(lock_) {
  std::lock_guard<std::mutex> lock(lock_);

  for (auto& link : SourceLinkSet(&object)) {
    if (auto ptr = link.object.lock()) {
      f(LinkHandle{.object = ptr,
                   .loudness_transform = link.loudness_transform,
                   .stream = link.stream,
                   .mixer = link.mixer});
    }
  }
}

size_t LinkMatrix::DestLinkCount(const AudioObject& object) {
  std::lock_guard<std::mutex> lock(lock_);

  return DestLinkSet(&object).size();
}

size_t LinkMatrix::SourceLinkCount(const AudioObject& object) {
  std::lock_guard<std::mutex> lock(lock_);

  return SourceLinkSet(&object).size();
}

void LinkMatrix::DestLinks(const AudioObject& object, std::vector<LinkHandle>* store)
    FXL_LOCKS_EXCLUDED(lock_) {
  std::lock_guard<std::mutex> lock(lock_);

  OnlyStrongLinks(DestLinkSet(&object), store);
}

void LinkMatrix::SourceLinks(const AudioObject& object, std::vector<LinkHandle>* store)
    FXL_LOCKS_EXCLUDED(lock_) {
  std::lock_guard<std::mutex> lock(lock_);

  OnlyStrongLinks(SourceLinkSet(&object), store);
}

bool LinkMatrix::AreLinked(const AudioObject& source, AudioObject& dest) FXL_LOCKS_EXCLUDED(lock_) {
  std::lock_guard<std::mutex> lock(lock_);

  auto dests = DestLinkSet(&source);

  return std::any_of(dests.begin(), dests.end(),
                     [&dest](auto candidate) { return candidate.key == &dest; });
}

void LinkMatrix::OnlyStrongLinks(LinkSet& link_set, std::vector<LinkHandle>* store) {
  store->clear();
  for (const auto& link : link_set) {
    if (auto ptr = link.object.lock()) {
      store->push_back(LinkHandle{.object = ptr,
                                  .loudness_transform = link.loudness_transform,
                                  .stream = link.stream,
                                  .mixer = link.mixer});
    }
  }
}

LinkMatrix::LinkSet& LinkMatrix::SourceLinkSet(const AudioObject* object) FXL_REQUIRE(lock_) {
  if (sources_.find(object) == sources_.end()) {
    sources_.insert({object, {}});
  }
  return sources_[object];
}

LinkMatrix::LinkSet& LinkMatrix::DestLinkSet(const AudioObject* object) FXL_REQUIRE(lock_) {
  if (dests_.find(object) == dests_.end()) {
    dests_.insert({object, {}});
  }

  return dests_[object];
}

}  // namespace media::audio
