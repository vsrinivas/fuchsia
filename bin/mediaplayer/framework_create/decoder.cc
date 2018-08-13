// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "garnet/bin/mediaplayer/decode/decoder.h"

#include "garnet/bin/mediaplayer/ffmpeg/ffmpeg_decoder_factory.h"
#include "garnet/bin/mediaplayer/fidl/fidl_decoder_factory.h"

namespace media_player {
namespace {

// A docoder factory that polls other decoder factories.
class CompositeDecoderFactory : public DecoderFactory {
 public:
  // Creates a composite decoder factory.
  static std::unique_ptr<CompositeDecoderFactory> Create();

  CompositeDecoderFactory();

  ~CompositeDecoderFactory() override;

  // Adds a child factory. Child factories are polled in the order they're
  // added. Calls to this method are not permitted when a |CreateDecoder|
  // operation is underway.
  void AddFactory(std::unique_ptr<DecoderFactory> factory);

  // DecoderFactory implementation.
  void CreateDecoder(
      const StreamType& stream_type,
      fit::function<void(std::shared_ptr<Decoder>)> callback) override;

 private:
  void ContinueCreateDecoder(
      std::vector<std::unique_ptr<DecoderFactory>>::iterator iter,
      const StreamType& stream_type,
      fit::function<void(std::shared_ptr<Decoder>)> callback);

  std::vector<std::unique_ptr<DecoderFactory>> children_;
};

// static
std::unique_ptr<CompositeDecoderFactory> CompositeDecoderFactory::Create() {
  return std::make_unique<CompositeDecoderFactory>();
}

CompositeDecoderFactory::CompositeDecoderFactory() {}

CompositeDecoderFactory::~CompositeDecoderFactory() {}

void CompositeDecoderFactory::AddFactory(
    std::unique_ptr<DecoderFactory> factory) {
  children_.push_back(std::move(factory));
}

void CompositeDecoderFactory::CreateDecoder(
    const StreamType& stream_type,
    fit::function<void(std::shared_ptr<Decoder>)> callback) {
  FXL_DCHECK(callback);

  ContinueCreateDecoder(children_.begin(), stream_type, std::move(callback));
}

void CompositeDecoderFactory::ContinueCreateDecoder(
    std::vector<std::unique_ptr<DecoderFactory>>::iterator iter,
    const StreamType& stream_type,
    fit::function<void(std::shared_ptr<Decoder>)> callback) {
  FXL_DCHECK(callback);

  if (iter == children_.end()) {
    callback(nullptr);
  }

  (*iter)->CreateDecoder(
      stream_type, [this, iter, &stream_type, callback = std::move(callback)](
                       std::shared_ptr<Decoder> decoder) mutable {
        if (decoder) {
          callback(decoder);
          return;
        }

        ContinueCreateDecoder(++iter, stream_type, std::move(callback));
      });
}

}  // namespace

std::unique_ptr<DecoderFactory> DecoderFactory::Create(
    component::StartupContext* startup_context) {
  auto parent_factory = CompositeDecoderFactory::Create();
  parent_factory->AddFactory(FidlDecoderFactory::Create(startup_context));
  parent_factory->AddFactory(FfmpegDecoderFactory::Create(startup_context));
  return parent_factory;
}

}  // namespace media_player
