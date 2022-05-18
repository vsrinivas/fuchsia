// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/ffmpeg/metadata.h"

extern "C" {
#include "libavformat/avformat.h"
}

namespace fmlib {
namespace {

const std::unordered_map<std::string, std::string> kMetadataLabelMap{
    {"title", fuchsia::audiovideo::METADATA_LABEL_TITLE},
    {"TITLE", fuchsia::audiovideo::METADATA_LABEL_TITLE},
    {"language", fuchsia::audiovideo::METADATA_LABEL_LANGUAGE},
    {"ENCODER", fuchsia::audiovideo::METADATA_LABEL_ENCODER},
    {"CREATION_TIME", fuchsia::audiovideo::METADATA_LABEL_CREATION_TIME},
    {"COMPOSER", fuchsia::audiovideo::METADATA_LABEL_COMPOSER},
    {"PUBLISHER", fuchsia::audiovideo::METADATA_LABEL_PUBLISHER},
    {"GENRE", fuchsia::audiovideo::METADATA_LABEL_GENRE},
    {"ARTIST", fuchsia::audiovideo::METADATA_LABEL_ARTIST},
    {"track", fuchsia::audiovideo::METADATA_LABEL_TRACK_NUMBER},
    {"album_artist", fuchsia::audiovideo::METADATA_LABEL_ALBUM_ARTIST},
    {"ALBUM", fuchsia::audiovideo::METADATA_LABEL_ALBUM},

    // These have been seen but have no corresponding fuchsia.audiovideo constants.
    //
    //{"ISVBR", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"MEDIAFOUNDATIONVERSION", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"DEVICECONFORMANCETEMPLATE", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/WMADRCAVERAGEREFERENCE", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/WMADRCAVERAGETARGET", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/UNIQUEFILEIDENTIFIER", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/PROVIDERSTYLE", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/ENCODINGTIME", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/PROVIDER", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/WMADRCPEAKREFERENCE", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/MEDIAPRIMARYCLASSID", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/YEAR", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/WMADRCPEAKTARGET", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WM/PROVIDERRATING", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WMFSDKNEEDED", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
    //{"WMFSDKVERSION", fuchsia::audiovideo::METADATA_LABEL_UNDEFINED},
};

const std::string kMetadataUnknownPropertyPrefix = "ffmpeg.";

}  // namespace

Metadata::Metadata(const fuchsia::audiovideo::Metadata& fidl) {
  for (auto& property : fidl.properties) {
    values_by_label_.emplace(property.label, property.value);
  }
}

fuchsia::audiovideo::Metadata Metadata::fidl() const {
  fuchsia::audiovideo::Metadata metadata;
  for (auto& pair : values_by_label_) {
    metadata.properties.push_back(
        fuchsia::audiovideo::Property{.label = pair.first, .value = pair.second});
  }

  return metadata;
}

fuchsia::audiovideo::MetadataPtr Metadata::fidl_ptr() const {
  if (values_by_label_.empty()) {
    return nullptr;
  }

  auto metadata = fuchsia::audiovideo::Metadata::New();
  for (auto& pair : values_by_label_) {
    metadata->properties.push_back(
        fuchsia::audiovideo::Property{.label = pair.first, .value = pair.second});
  }

  return metadata;
}

void Metadata::Merge(AVDictionary* source) {
  if (source == nullptr) {
    return;
  }

  for (AVDictionaryEntry* entry = av_dict_get(source, "", nullptr, AV_DICT_IGNORE_SUFFIX);
       entry != nullptr; entry = av_dict_get(source, "", entry, AV_DICT_IGNORE_SUFFIX)) {
    std::string label = entry->key;
    auto iter = kMetadataLabelMap.find(label);
    if (iter != kMetadataLabelMap.end()) {
      // Store the property under its fuchsia.audiovideo label.
      label = iter->second;
    } else {
      // Store the property under "ffmpeg.<ffmpeg label>".
      std::string temp;
      temp.reserve(kMetadataUnknownPropertyPrefix.size() + label.size());
      temp += kMetadataUnknownPropertyPrefix;
      temp += label;
      label = std::move(temp);
    }

    values_by_label_.emplace(label, entry->value);
  }
}

}  // namespace fmlib
