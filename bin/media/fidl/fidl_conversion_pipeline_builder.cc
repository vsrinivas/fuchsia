// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/fidl/fidl_conversion_pipeline_builder.h"

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "garnet/bin/media/framework/formatting.h"
#include "garnet/bin/media/framework/types/audio_stream_type.h"
#include "garnet/bin/media/framework/types/stream_type.h"
#include "garnet/bin/media/framework/types/video_stream_type.h"
#include "garnet/bin/media/util/callback_joiner.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/media/fidl/media_service.fidl.h"
#include "lib/media/fidl/media_type_converter.fidl.h"
#include "lib/media/flog/flog.h"

namespace media {

namespace {

// Builds a conversion pipeline and connects it to a supplied producer, consumer
// or both.
class Builder {
 public:
  // Constructs a builder that builds a pipeline that converts packets of type
  // |type| to packets whose type is in |goal_type_sets|, calling |callback| on
  // completion. If the pipeline is successfully built, it will be connected
  // to the producer obtained from |producer_getter| and the consumer obtained
  // from |consumer_getter|. If the pipeline cannot be built, neither getter is
  // called. On successful completion, the callback supplies the consumer getter
  // for the pipeline if |producer_getter| was null, and the callback supplies
  // the producer getter for the pipeline if |consumer_getter| was null. The
  // bool passed to the callback indicates whether the build was successful.
  // The stream type passed to the callback is the converted stream type if
  // the build was successful or |type| if it was not.
  Builder(const MediaServicePtr& media_service,
          const std::vector<std::unique_ptr<StreamTypeSet>>& goal_type_sets,
          const ProducerGetter& producer_getter,
          const ConsumerGetter& consumer_getter,
          std::unique_ptr<StreamType> type,
          const std::function<void(bool succeeded,
                                   const ConsumerGetter&,
                                   const ProducerGetter&,
                                   std::unique_ptr<StreamType>,
                                   std::vector<zx_koid_t>)>& callback)
      : media_service_(media_service),
        goal_type_sets_(goal_type_sets),
        producer_getter_(producer_getter),
        consumer_getter_(consumer_getter),
        original_type_(std::move(type)),
        callback_(callback),
        type_(&original_type_) {
    FXL_DCHECK(media_service_);
    FXL_DCHECK(original_type_);
    FXL_DCHECK(callback_);
  }

  // Attempts to advance the build process. If the current type is already in
  // the goal set, this method calls |Succeed|. Otherwise, this method either
  // delegates to one of the private AddConverterX methods or calls |Fail| in
  // case none of those methods is applicable.
  void AddConverters();

 private:
  // Determines if the goal type sets include |stream_type|.
  bool GoalTypeSetsInclude(const StreamType& stream_type) const;

  // Determines if the goal type sets include a stream type with the specified
  // encoding.
  bool GoalTypeSetsIncludeEncoding(const std::string& encoding) const;

  // Adds |converter| to the pipeline and calls |AddConverters|.
  void AddConverter(MediaTypeConverterPtr converter);

  // Attempts to advance the build process given that the current type is a
  // compressed audio type. Calls |AddConverter| if successful or |Fail| if
  // not.
  void AddConverterForCompressedAudio();

  // Attempts to advance the build process given that the current type is a
  // compressed video type. Calls |AddConverter| if successful or |Fail| if
  // not.
  void AddConverterForCompressedVideo();

  // Attempts to advance the build process given that the current type is an
  // uncompressed audio type and can be converted to a type in |goal_type_set|.
  // Calls |AddConverter| if successful or |Fail| if not.
  void AddConverterForLpcm(const AudioStreamTypeSet& goal_type_set);

  // Attempts to advance the build process given that the current type is an
  // uncompressed audio type. Calls the other overload of |AddConverterForLpcm|
  // if successful or |Fail| if not.
  void AddConverterForLpcm();

  // Connects the converters, calls |callback_| indicating success and deletes
  // this.
  void Succeed();

  // Calls |callback_| indicating failure and deletes this.
  void Fail();

  const MediaServicePtr& media_service_;
  const std::vector<std::unique_ptr<StreamTypeSet>>& goal_type_sets_;
  ProducerGetter producer_getter_;
  ConsumerGetter consumer_getter_;
  std::unique_ptr<StreamType> original_type_;
  std::function<void(bool succeeded,
                     const ConsumerGetter&,
                     const ProducerGetter&,
                     std::unique_ptr<StreamType>,
                     std::vector<zx_koid_t>)>
      callback_;
  std::unique_ptr<StreamType> current_type_;
  std::unique_ptr<StreamType>* type_;  // &original_type_ or &current_type_
  std::vector<MediaTypeConverterPtr> converters_;
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
  int best_score = 0;

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

bool Builder::GoalTypeSetsInclude(const StreamType& stream_type) const {
  for (auto& goal_type_set : goal_type_sets_) {
    if (goal_type_set->Includes(stream_type)) {
      return true;
    }
  }

  return false;
}

bool Builder::GoalTypeSetsIncludeEncoding(const std::string& encoding) const {
  for (auto& goal_type_set : goal_type_sets_) {
    if (goal_type_set->IncludesEncoding(encoding)) {
      return true;
    }
  }

  return false;
}

void Builder::AddConverter(MediaTypeConverterPtr converter) {
  converter->GetOutputType([this](MediaTypePtr output_type) {
    current_type_ = output_type.To<std::unique_ptr<StreamType>>();
    type_ = &current_type_;
    AddConverters();
  });

  converters_.push_back(std::move(converter));
}

void Builder::AddConverterForCompressedAudio() {
  FXL_DCHECK((*type_)->medium() == StreamType::Medium::kAudio);
  FXL_DCHECK((*type_)->audio() != nullptr);
  FXL_DCHECK((*type_)->encoding() != StreamType::kAudioEncodingLpcm);

  // See if LPCM audio is in the goal set.
  if (!GoalTypeSetsIncludeEncoding(StreamType::kAudioEncodingLpcm)) {
    // TODO(dalesat): Support a different compressed output type by
    // transcoding.
    Fail();
    return;
  }

  MediaTypeConverterPtr decoder;
  media_service_->CreateDecoder(MediaType::From((*type_)),
                                decoder.NewRequest());

  AddConverter(std::move(decoder));
}

void Builder::AddConverterForCompressedVideo() {
  FXL_DCHECK((*type_)->medium() == StreamType::Medium::kVideo);
  FXL_DCHECK((*type_)->video() != nullptr);
  FXL_DCHECK((*type_)->encoding() != StreamType::kVideoEncodingUncompressed);

  // See if uncompressed video is in the goal set.
  if (!GoalTypeSetsIncludeEncoding(StreamType::kVideoEncodingUncompressed)) {
    // TODO(dalesat): Support a different compressed output type by
    // transcoding.
    Fail();
    return;
  }

  MediaTypeConverterPtr decoder;
  media_service_->CreateDecoder(MediaType::From((*type_)),
                                decoder.NewRequest());

  AddConverter(std::move(decoder));
}

void Builder::AddConverterForLpcm(const AudioStreamTypeSet& goal_type_set) {
  FXL_DCHECK((*type_)->medium() == StreamType::Medium::kAudio);
  FXL_DCHECK((*type_)->audio() != nullptr);
  FXL_DCHECK((*type_)->encoding() == StreamType::kAudioEncodingLpcm);

  // TODO(dalesat): Room for more intelligence here wrt transform ordering and
  // transforms that handle more than one conversion.

  if ((*type_)->audio()->sample_format() != goal_type_set.sample_format() &&
      goal_type_set.sample_format() != AudioStreamType::SampleFormat::kAny) {
    MediaTypeConverterPtr reformatter;
    media_service_->CreateLpcmReformatter(
        MediaType::From((*type_)), Convert(goal_type_set.sample_format()),
        reformatter.NewRequest());

    AddConverter(std::move(reformatter));
    return;
  }

  if (!goal_type_set.channels().contains((*type_)->audio()->channels())) {
    // TODO(dalesat): Insert mixdown/up transform.
  } else if (!goal_type_set.frames_per_second().contains(
                 (*type_)->audio()->frames_per_second())) {
    // TODO(dalesat): Insert resampler.
  } else {
    // We only get here if there's some attribute of audio types that isn't
    // covered above. That shouldn't happen.
    FXL_DCHECK(false) << "Can't determine what conversion is required";
  }

  Fail();
}

void Builder::AddConverterForLpcm() {
  FXL_DCHECK((*type_)->medium() == StreamType::Medium::kAudio);
  FXL_DCHECK((*type_)->audio() != nullptr);
  FXL_DCHECK((*type_)->encoding() == StreamType::kAudioEncodingLpcm);

  const std::unique_ptr<StreamTypeSet>* best =
      FindBestLpcm(*(*type_)->audio(), goal_type_sets_);
  if (best == nullptr) {
    // TODO(dalesat): Support a compressed output type by encoding.
    FXL_LOG(WARNING) << "Conversion requires encoding - not supported";
    Fail();
    return;
  }

  FXL_DCHECK((*best)->medium() == StreamType::Medium::kAudio);

  return AddConverterForLpcm(*(*best)->audio());
}

void Builder::AddConverters() {
  if ((*type_)->encoding() == StreamType::kMediaEncodingUnsupported) {
    FXL_DLOG(WARNING) << "Conversion not supported for encoding "
                      << StreamType::kMediaEncodingUnsupported;
    Fail();
    return;
  }

  if (GoalTypeSetsInclude(**type_)) {
    Succeed();
    return;
  }

  switch ((*type_)->medium()) {
    case StreamType::Medium::kAudio:
      if ((*type_)->encoding() == StreamType::kAudioEncodingLpcm) {
        AddConverterForLpcm();
      } else {
        AddConverterForCompressedAudio();
      }
      break;

    case StreamType::Medium::kVideo:
      if ((*type_)->encoding() == StreamType::kVideoEncodingUncompressed) {
        FXL_LOG(WARNING) << "Conversion of uncompressed video not supported";
        Fail();
      } else {
        AddConverterForCompressedVideo();
      }
      break;

    default:
      FXL_LOG(WARNING) << "Conversion not supported for medium "
                       << (*type_)->medium();
      Fail();
  }
}

void Builder::Succeed() {
  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  MediaPacketProducerPtr producer;
  if (producer_getter_ && (consumer_getter_ || !converters_.empty())) {
    // We have a producer getter and something to connect the producer to.
    // Get the producer.
    producer_getter_(producer.NewRequest());
  }

  for (auto& converter : converters_) {
    if (producer) {
      // We need to connect producer to this converter's consumer, either
      // because this isn't the first converter or because we were provided a
      // ProducerGetter.
      MediaPacketConsumerPtr consumer;
      converter->GetPacketConsumer(consumer.NewRequest());

      callback_joiner->Spawn();
      // Capture producer to keep it alive through the callback.
      producer->Connect(std::move(consumer), fxl::MakeCopyable([
                          callback_joiner, producer = std::move(producer)
                        ]() { callback_joiner->Complete(); }));
    }

    if (converter.get() != converters_.back().get() || consumer_getter_) {
      // We have a consumer to connect this converter's producer to. Get the
      // producer.
      converter->GetPacketProducer(producer.NewRequest());
    }
  }

  if (consumer_getter_ && producer) {
    // We have a consumer getter and something to connect the consumer to.
    // Get the consumer and connect it to the producer.
    FXL_DCHECK(producer);
    MediaPacketConsumerPtr consumer;
    consumer_getter_(consumer.NewRequest());

    callback_joiner->Spawn();
    // Capture producer to keep it alive through the callback.
    producer->Connect(std::move(consumer), fxl::MakeCopyable([
                        callback_joiner, producer = std::move(producer)
                      ]() { callback_joiner->Complete(); }));
  }

  callback_joiner->WhenJoined([this]() {
    if (converters_.empty()) {
      // No converters required. Return the getters that weren't used. If both
      // getters were provided, we've already connected the producer and
      // consumer together and we don't want to return the getters.
      if (producer_getter_ && consumer_getter_) {
        producer_getter_ = nullptr;
        consumer_getter_ = nullptr;
      }

      callback_(true, consumer_getter_, producer_getter_, std::move(*type_),
                std::vector<zx_koid_t>());
      delete this;
      return;
    }

    std::vector<zx_koid_t> converter_koids;
    converter_koids.reserve(converters_.size());
    for (MediaTypeConverterPtr& ptr : converters_) {
      converter_koids.push_back(FLOG_PTR_KOID(ptr));
    }

    if (!producer_getter_ && !consumer_getter_ && converters_.size() == 1) {
      // Only one converter was required, and we weren't given either getter.
      // This is a special case, because we need to create two getters that
      // capture pointers to the same converter.
      MediaTypeConverterPtr* ptr_ptr = new MediaTypeConverterPtr();
      *ptr_ptr = std::move(converters_.front());
      std::shared_ptr<MediaTypeConverterPtr> shared_converter_ptr(ptr_ptr);

      callback_(
          true,
          [shared_converter_ptr](
              fidl::InterfaceRequest<MediaPacketConsumer> request) {
            (*shared_converter_ptr)->GetPacketConsumer(std::move(request));
          },
          [shared_converter_ptr](
              fidl::InterfaceRequest<MediaPacketProducer> request) {
            (*shared_converter_ptr)->GetPacketProducer(std::move(request));
          },
          std::move(*type_), std::move(converter_koids));
      delete this;
      return;
    }

    ConsumerGetter consumer_getter_to_return;
    if (!producer_getter_) {
      // A ProducerGetter wasn't provided, so the caller will need a
      // ConsumerGetter to connect a producer later on.
      consumer_getter_to_return =
          fxl::MakeCopyable([converter = std::move(converters_.front())](
              fidl::InterfaceRequest<MediaPacketConsumer> request) {
            converter->GetPacketConsumer(std::move(request));
          });
    }

    ProducerGetter producer_getter_to_return;
    if (!consumer_getter_) {
      // A ConsumerGetter wasn't provided, so the caller will need a
      // ProducerGetter to connect a consumer later on.
      producer_getter_to_return =
          fxl::MakeCopyable([converter = std::move(converters_.back())](
              fidl::InterfaceRequest<MediaPacketProducer> request) {
            converter->GetPacketProducer(std::move(request));
          });
    }

    callback_(true, consumer_getter_to_return, producer_getter_to_return,
              std::move(*type_), std::move(converter_koids));
    delete this;
  });
}

void Builder::Fail() {
  callback_(false, nullptr, nullptr, std::move(original_type_),
            std::vector<zx_koid_t>());
  delete this;
}

}  // namespace

void BuildFidlConversionPipeline(
    const MediaServicePtr& media_service,
    const std::vector<std::unique_ptr<StreamTypeSet>>& goal_type_sets,
    const ProducerGetter& producer_getter,
    const ConsumerGetter& consumer_getter,
    std::unique_ptr<StreamType> type,
    const std::function<void(bool succeeded,
                             const ConsumerGetter&,
                             const ProducerGetter&,
                             std::unique_ptr<StreamType>,
                             std::vector<zx_koid_t>)>& callback) {
  FXL_DCHECK(media_service);
  FXL_DCHECK(type);
  FXL_DCHECK(callback);

  Builder* builder = new Builder(media_service, goal_type_sets, producer_getter,
                                 consumer_getter, std::move(type), callback);
  builder->AddConverters();
}

}  // namespace media
