// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_CONVERSION_PIPELINE_BUILDER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_CONVERSION_PIPELINE_BUILDER_H_

#include "garnet/bin/media/media_player/decode/decoder.h"
#include "garnet/bin/media/media_player/framework/graph.h"
#include "garnet/bin/media/media_player/framework/packet.h"
#include "garnet/bin/media/media_player/framework/types/stream_type.h"

namespace media_player {

// Attempts to add transforms to the given pipeline to convert in_type to a
// type compatible with out_type_sets. If it succeeds, returns true, updates
// *output and delivers the resulting output type via *out_type. If it fails,
// returns false, sets *out_type to nullptr and leaves *output unchanged.
bool BuildConversionPipeline(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef* output,
    std::unique_ptr<StreamType>* out_type);

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_PLAYER_CONVERSION_PIPELINE_BUILDER_H_
