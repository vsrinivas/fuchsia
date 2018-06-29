// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/player/conversion_pipeline_builder.h"

#include "garnet/bin/media/media_player/decode/decoder.h"
#include "garnet/bin/media/media_player/framework/formatting.h"

namespace media_player {

namespace {

enum class AddResult {
  kFailed,      // Can't convert.
  kProgressed,  // Added a conversion transform.
  kFinished     // Done adding conversion transforms.
};

// Produces a score for in_type with respect to out_type_set. The score
// is used to compare type sets to see which represents the best goal for
// conversion. Higher scores are preferred. A score of zero indicates that
// in_type is incompatible with out_type_set.
int Score(const AudioStreamType& in_type,
          const AudioStreamTypeSet& out_type_set) {
  // TODO(dalesat): Plenty of room for more subtlety here. Maybe actually
  // measure conversion costs (cpu, quality, etc) and reflect them here.

  int score = 1;  // We can convert anything, so 1 is the minimum score.

  if (in_type.sample_format() == out_type_set.sample_format() ||
      out_type_set.sample_format() == AudioStreamType::SampleFormat::kAny) {
    // Prefer not to convert sample format.
    score += 10;
  } else {
    // Prefer higher-quality formats.
    switch (out_type_set.sample_format()) {
      case AudioStreamType::SampleFormat::kUnsigned8:
        break;
      case AudioStreamType::SampleFormat::kSigned16:
        score += 1;
        break;
      case AudioStreamType::SampleFormat::kSigned24In32:
        score += 2;
        break;
      case AudioStreamType::SampleFormat::kFloat:
        score += 3;
        break;
      default:
        FXL_DCHECK(false) << "unsupported sample format "
                          << out_type_set.sample_format();
    }
  }

  if (out_type_set.channels().contains(in_type.channels())) {
    // Prefer not to mixdown/up.
    score += 10;
  } else {
    return 0;  // TODO(dalesat): Remove when we have mixdown/up.
  }

  if (out_type_set.frames_per_second().contains(in_type.frames_per_second())) {
    // Very much prefer not to resample.
    score += 50;
  } else {
    return 0;  // TODO(dalesat): Remove when we have resamplers.
  }

  return score;
}

// Finds the stream type set that best matches in_type.
const std::unique_ptr<StreamTypeSet>* FindBestLpcm(
    const AudioStreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets) {
  const std::unique_ptr<StreamTypeSet>* best = nullptr;
  int best_score = -1;

  for (const std::unique_ptr<StreamTypeSet>& out_type_set : out_type_sets) {
    if (out_type_set->medium() == StreamType::Medium::kAudio &&
        out_type_set->IncludesEncoding(StreamType::kAudioEncodingLpcm)) {
      int score = Score(in_type, *out_type_set->audio());
      if (best_score < score) {
        best_score = score;
        best = &out_type_set;
      }
    }
  }

  return best;
}

// Attempts to add transforms to the pipeline given an input compressed audio
// stream type with (in_type) and the set of output types we need to convert to
// (out_type_sets). If the call succeeds, *out_type is set to the new output
// type. Otherwise, *out_type is set to nullptr.
AddResult AddTransformsForCompressedAudio(
    const AudioStreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef* output,
    std::unique_ptr<StreamType>* out_type) {
  FXL_DCHECK(out_type);
  FXL_DCHECK(graph);
  FXL_DCHECK(decoder_factory);

  // See if we have a matching audio type.
  for (const std::unique_ptr<StreamTypeSet>& out_type_set : out_type_sets) {
    if (out_type_set->medium() == StreamType::Medium::kAudio) {
      if (out_type_set->audio()->Includes(in_type)) {
        // No transform needed.
        *out_type = in_type.Clone();
        return AddResult::kFinished;
      }
      // TODO(dalesat): Support a different compressed output type by
      // transcoding.
    }
  }

  // Find the best LPCM output type.
  const std::unique_ptr<StreamTypeSet>* best =
      FindBestLpcm(in_type, out_type_sets);
  if (best == nullptr) {
    // No candidates found.
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  FXL_DCHECK((*best)->medium() == StreamType::Medium::kAudio);
  FXL_DCHECK((*best)->IncludesEncoding(StreamType::kAudioEncodingLpcm));

  // Need to decode. Create a decoder and go from there.
  std::shared_ptr<Decoder> decoder;
  Result result = decoder_factory->CreateDecoder(in_type, &decoder);
  if (result != Result::kOk) {
    // No decoder found.
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  *output = graph->ConnectOutputToNode(*output, graph->Add(decoder)).output();
  *out_type = decoder->output_stream_type();

  return AddResult::kProgressed;
}

// Attempts to add transforms to the pipeline given an input compressed video
// stream type with (in_type) and the set of output types we need to convert to
// (out_type_sets). If the call succeeds, *out_type is set to the new output
// type. Otherwise, *out_type is set to nullptr.
AddResult AddTransformsForCompressedVideo(
    const VideoStreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef* output,
    std::unique_ptr<StreamType>* out_type) {
  FXL_DCHECK(out_type);
  FXL_DCHECK(graph);
  FXL_DCHECK(decoder_factory);

  // TODO(dalesat): See if we already have a matching video type.

  // Need to decode. Create a decoder and go from there.
  std::shared_ptr<Decoder> decoder;
  Result result = decoder_factory->CreateDecoder(in_type, &decoder);
  if (result != Result::kOk) {
    // No decoder found.
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  *output = graph->ConnectOutputToNode(*output, graph->Add(decoder)).output();
  *out_type = decoder->output_stream_type();

  return AddResult::kProgressed;
}

// Attempts to add transforms to the pipeline given an input LPCM stream type
// (in_type) and the output lpcm stream type set for the type we need to
// convert to (out_type_set). If the call succeeds, *out_type is set to the new
// output type. Otherwise, *out_type is set to nullptr.
AddResult AddTransformsForLpcm(const AudioStreamType& in_type,
                               const AudioStreamTypeSet& out_type_set,
                               Graph* graph, OutputRef* output,
                               std::unique_ptr<StreamType>* out_type) {
  FXL_DCHECK(graph);
  FXL_DCHECK(out_type);

  // TODO(dalesat): Room for more intelligence here wrt transform ordering and
  // transforms that handle more than one conversion.
  if (in_type.sample_format() != out_type_set.sample_format() &&
      out_type_set.sample_format() != AudioStreamType::SampleFormat::kAny) {
    FXL_DCHECK(false)
        << "conversion requires audio format change - not supported";
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  if (!out_type_set.channels().contains(in_type.channels())) {
    // TODO(dalesat): Insert mixdown/up transform.
    FXL_DCHECK(false) << "conversion requires mixdown/up - not supported";
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  if (!out_type_set.frames_per_second().contains(in_type.frames_per_second())) {
    // TODO(dalesat): Insert resampler.
    FXL_DCHECK(false) << "conversion requires resampling - not supported";
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  // Build the resulting media type.
  *out_type = AudioStreamType::Create(
      StreamType::kAudioEncodingLpcm, nullptr,
      out_type_set.sample_format() == AudioStreamType::SampleFormat::kAny
          ? in_type.sample_format()
          : out_type_set.sample_format(),
      in_type.channels(), in_type.frames_per_second());

  return AddResult::kFinished;
}

// Attempts to add transforms to the pipeline given an input audio stream type
// witn lpcm encoding (in_type) and the set of output types we need to convert
// to (out_type_sets). If the call succeeds, *out_type is set to the new
// output type. Otherwise, *out_type is set to nullptr.
AddResult AddTransformsForLpcm(
    const AudioStreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, OutputRef* output, std::unique_ptr<StreamType>* out_type) {
  FXL_DCHECK(graph);
  FXL_DCHECK(out_type);

  const std::unique_ptr<StreamTypeSet>* best =
      FindBestLpcm(in_type, out_type_sets);
  if (best == nullptr) {
    // TODO(dalesat): Support a compressed output type by encoding.
    FXL_DCHECK(false) << "conversion using encoder not supported";
    *out_type = nullptr;
    return AddResult::kFailed;
  }

  FXL_DCHECK((*best)->medium() == StreamType::Medium::kAudio);

  return AddTransformsForLpcm(in_type, *(*best)->audio(), graph, output,
                              out_type);
}

// Attempts to add transforms to the pipeline given an input media type of any
// medium and encoding (in_type) and the set of output types we need to
// convert to (out_type_sets). If the call succeeds, *out_type is set to the new
// output type. Otherwise, *out_type is set to nullptr.
AddResult AddTransforms(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef* output,
    std::unique_ptr<StreamType>* out_type) {
  FXL_DCHECK(graph);
  FXL_DCHECK(decoder_factory);
  FXL_DCHECK(out_type);

  switch (in_type.medium()) {
    case StreamType::Medium::kAudio:
      if (in_type.encoding() == StreamType::kAudioEncodingLpcm) {
        return AddTransformsForLpcm(*in_type.audio(), out_type_sets, graph,
                                    output, out_type);
      } else {
        return AddTransformsForCompressedAudio(*in_type.audio(), out_type_sets,
                                               graph, decoder_factory, output,
                                               out_type);
      }
    case StreamType::Medium::kVideo:
      if (in_type.encoding() == StreamType::kVideoEncodingUncompressed) {
        *out_type = in_type.Clone();
        return AddResult::kFinished;
      } else {
        return AddTransformsForCompressedVideo(*in_type.video(), out_type_sets,
                                               graph, decoder_factory, output,
                                               out_type);
      }
    default:
      FXL_DCHECK(false) << "conversion not supported for medium"
                        << in_type.medium();
      *out_type = nullptr;
      return AddResult::kFailed;
  }
}

}  // namespace

bool BuildConversionPipeline(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef* output,
    std::unique_ptr<StreamType>* out_type) {
  FXL_DCHECK(graph);
  FXL_DCHECK(decoder_factory);
  FXL_DCHECK(output);
  FXL_DCHECK(out_type);

  OutputRef out = *output;
  const StreamType* type_to_convert = &in_type;
  std::unique_ptr<StreamType> converted_type;
  while (true) {
    switch (AddTransforms(*type_to_convert, out_type_sets, graph,
                          decoder_factory, &out, &converted_type)) {
      case AddResult::kFailed:
        // Failed to find a suitable conversion. Return the pipeline to its
        // original state.
        graph->RemoveNodesConnectedToOutput(*output);
        *out_type = nullptr;
        return false;
      case AddResult::kProgressed:
        // Made progress. Continue.
        break;
      case AddResult::kFinished:
        // No further conversion required.
        *output = out;
        *out_type = std::move(converted_type);
        return true;
    }

    type_to_convert = converted_type.get();
  }
}

}  // namespace media_player
