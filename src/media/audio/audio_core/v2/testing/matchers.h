// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TESTING_MATCHERS_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TESTING_MATCHERS_H_

#include <fidl/fuchsia.audio.mixer/cpp/natural_types.h>

#include <gmock/gmock.h>

#include "src/media/audio/lib/format2/format.h"

namespace media_audio {

// Checks if a fuchsia_audio::Format (arg) matches a media_audio::Format (want).
MATCHER_P(FidlFormatEq, want, "") {
  if (!arg) {
    *result_listener << "missing format";
    return false;
  }
  if (auto got = Format::CreateOrDie(*arg); got != want) {
    *result_listener << "got format " << got << " != expected format " << want;
    return false;
  }
  return true;
}

// Checks if a fuchsia_mediastreams::AudioFormat (arg) matches a media_audio::Format (want).
MATCHER_P(LegacyFidlFormatEq, want, "") {
  if (!arg) {
    *result_listener << "missing format";
    return false;
  }
  if (auto got = Format::CreateLegacyOrDie(*arg); got != want) {
    *result_listener << "got format " << got << " != expected format " << want;
    return false;
  }
  return true;
}

// Checks if a fuchsia_audio_mixer::ReferenceClock (arg) is valid.
MATCHER_P(ValidReferenceClock, want_domain, "") {
  if (!arg) {
    *result_listener << "missing reference clock";
    return false;
  }
  if (!arg->handle() || !arg->handle()->is_valid()) {
    *result_listener << "missing reference clock handle";
    return false;
  }
  if (!arg->domain()) {
    *result_listener << "missing reference clock domain";
    return false;
  }
  if (*arg->domain() != want_domain) {
    *result_listener << "got domain " << *arg->domain() << " expected " << want_domain;
    return false;
  }
  return true;
}

// Checks if a FakeGraphServer::CallType is an edge connecting source -> dest.
MATCHER_P2(CreateEdgeEq, want_source, want_dest, "") {
  auto call = std::get_if<fuchsia_audio_mixer::GraphCreateEdgeRequest>(&arg);
  if (!call) {
    *result_listener << "got " << arg.index() << ", wanted a CreateEdge call";
    return false;
  }
  if (!call->source_id() || !call->dest_id()) {
    *result_listener << "missing ids";
    return false;
  }
  if (*call->source_id() != want_source || *call->dest_id() != want_dest) {
    *result_listener << "got edge " << *call->source_id() << "->" << *call->dest_id()
                     << " want edge " << want_source << "->" << want_dest;
    return false;
  }
  return true;
}

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V2_TESTING_MATCHERS_H_
