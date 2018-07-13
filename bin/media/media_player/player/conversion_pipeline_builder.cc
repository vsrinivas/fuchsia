// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/player/conversion_pipeline_builder.h"

#include "garnet/bin/media/media_player/decode/decoder.h"
#include "garnet/bin/media/media_player/framework/formatting.h"

namespace media_player {

namespace {

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

class Builder {
 public:
  Builder(const StreamType& in_type,
          const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
          Graph* graph, DecoderFactory* decoder_factory, OutputRef output,
          fit::function<void(OutputRef, std::unique_ptr<StreamType>)> callback);

  ~Builder() = default;

  void Build();

 private:
  void Succeed();

  void Fail();

  void AddTransformsForCompressedAudio(const AudioStreamType& audio_type);

  void AddTransformsForCompressedVideo(const VideoStreamType& video_type);

  void AddDecoder();

  void AddTransformsForLpcm(const AudioStreamType& audio_type,
                            const AudioStreamTypeSet& out_type_set);

  void AddTransformsForLpcm(const AudioStreamType& audio_type);

  std::unique_ptr<StreamType> type_;
  const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets_;
  Graph* graph_;
  DecoderFactory* decoder_factory_;
  OutputRef output_;
  const OutputRef original_output_;
  const fit::function<void(OutputRef, std::unique_ptr<StreamType>)> callback_;
};

Builder::Builder(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef output,
    fit::function<void(OutputRef, std::unique_ptr<StreamType>)> callback)
    : type_(in_type.Clone()),
      out_type_sets_(out_type_sets),
      graph_(graph),
      decoder_factory_(decoder_factory),
      output_(output),
      original_output_(output),
      callback_(std::move(callback)) {}

void Builder::Build() {
  switch (type_->medium()) {
    case StreamType::Medium::kAudio:
      if (type_->encoding() == StreamType::kAudioEncodingLpcm) {
        AddTransformsForLpcm(*type_->audio());
      } else {
        AddTransformsForCompressedAudio(*type_->audio());
      }
      break;
    case StreamType::Medium::kVideo:
      if (type_->encoding() == StreamType::kVideoEncodingUncompressed) {
        Succeed();
      } else {
        AddTransformsForCompressedVideo(*type_->video());
      }
      break;
    default:
      FXL_DCHECK(false) << "conversion not supported for medium"
                        << type_->medium();
      Fail();
      break;
  }
}

void Builder::Succeed() {
  callback_(output_, type_->Clone());
  delete this;
}

void Builder::Fail() {
  graph_->RemoveNodesConnectedToOutput(original_output_);
  callback_(original_output_, nullptr);
  delete this;
}

void Builder::AddTransformsForCompressedAudio(
    const AudioStreamType& audio_type) {
  // See if we have a matching audio type.
  for (const std::unique_ptr<StreamTypeSet>& out_type_set : out_type_sets_) {
    if (out_type_set->medium() == StreamType::Medium::kAudio) {
      if (out_type_set->audio()->Includes(audio_type)) {
        // No transform needed.
        type_ = audio_type.Clone();
        Succeed();
      }
      // TODO(dalesat): Support a different compressed output type by
      // transcoding.
    }
  }

  // Find the best LPCM output type.
  const std::unique_ptr<StreamTypeSet>* best =
      FindBestLpcm(audio_type, out_type_sets_);
  if (best == nullptr) {
    // No candidates found.
    Fail();
    return;
  }

  FXL_DCHECK((*best)->medium() == StreamType::Medium::kAudio);
  FXL_DCHECK((*best)->IncludesEncoding(StreamType::kAudioEncodingLpcm));

  // Need to decode. Create a decoder and go from there.
  AddDecoder();
}

void Builder::AddTransformsForCompressedVideo(
    const VideoStreamType& video_type) {
  // TODO(dalesat): See if we already have a matching video type.

  AddDecoder();
}

void Builder::AddDecoder() {
  decoder_factory_->CreateDecoder(
      *type_, [this](std::shared_ptr<Decoder> decoder) {
        if (!decoder) {
          // No decoder found.
          Fail();
          return;
        }

        output_ =
            graph_->ConnectOutputToNode(output_, graph_->Add(decoder)).output();
        type_ = decoder->output_stream_type();

        Build();
      });
}

void Builder::AddTransformsForLpcm(const AudioStreamType& audio_type,
                                   const AudioStreamTypeSet& out_type_set) {
  if (audio_type.sample_format() != out_type_set.sample_format() &&
      out_type_set.sample_format() != AudioStreamType::SampleFormat::kAny) {
    // TODO(dalesat): Insert sample format converter.
    FXL_DCHECK(false)
        << "conversion requires sample format change - not supported";
    Fail();
    return;
  }

  if (!out_type_set.channels().contains(audio_type.channels())) {
    // TODO(dalesat): Insert mixdown/up transform.
    FXL_DCHECK(false) << "conversion requires mixdown/up - not supported";
    Fail();
    return;
  }

  if (!out_type_set.frames_per_second().contains(
          audio_type.frames_per_second())) {
    // TODO(dalesat): Insert resampler.
    FXL_DCHECK(false) << "conversion requires resampling - not supported";
    Fail();
    return;
  }

  // Build the resulting media type.
  type_ = AudioStreamType::Create(
      StreamType::kAudioEncodingLpcm, nullptr,
      out_type_set.sample_format() == AudioStreamType::SampleFormat::kAny
          ? audio_type.sample_format()
          : out_type_set.sample_format(),
      audio_type.channels(), audio_type.frames_per_second());

  Succeed();
}

void Builder::AddTransformsForLpcm(const AudioStreamType& audio_type) {
  const std::unique_ptr<StreamTypeSet>* best =
      FindBestLpcm(audio_type, out_type_sets_);
  if (best == nullptr) {
    // TODO(dalesat): Support a compressed output type by encoding.
    FXL_DCHECK(false) << "conversion using encoder not supported";
    Fail();
    return;
  }

  FXL_DCHECK((*best)->medium() == StreamType::Medium::kAudio);

  AddTransformsForLpcm(audio_type, *(*best)->audio());
}

}  // namespace

void BuildConversionPipeline(
    const StreamType& in_type,
    const std::vector<std::unique_ptr<StreamTypeSet>>& out_type_sets,
    Graph* graph, DecoderFactory* decoder_factory, OutputRef output,
    fit::function<void(OutputRef, std::unique_ptr<StreamType>)> callback) {
  FXL_DCHECK(graph);
  FXL_DCHECK(decoder_factory);
  FXL_DCHECK(output);
  FXL_DCHECK(callback);

  auto builder = new Builder(in_type, out_type_sets, graph, decoder_factory,
                             output, std::move(callback));
  builder->Build();
}

}  // namespace media_player
