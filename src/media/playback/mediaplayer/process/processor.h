// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_PROCESS_PROCESSOR_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_PROCESS_PROCESSOR_H_

#include "src/media/playback/mediaplayer/graph/nodes/node.h"
#include "src/media/playback/mediaplayer/graph/service_provider.h"
#include "src/media/playback/mediaplayer/graph/types/stream_type.h"

namespace media_player {

// Abstract base class for nodes that process sstreams.
class Processor : public Node {
 public:
  ~Processor() override {}

  // Sets the type of the stream the processor will consume. This method is used primarily for
  // 'injected' decryptors, which are generally created before the input type is known. Decoders
  // don't require a call to this method, but are not harmed by it.
  virtual void SetInputStreamType(const StreamType& stream_type) = 0;

  // Returns the type of the stream the processor will produce.
  virtual std::unique_ptr<StreamType> output_stream_type() const = 0;
};

// Abstract base class for decoder factories.
class DecoderFactory {
 public:
  // Creates a decoder factory.
  static std::unique_ptr<DecoderFactory> Create(ServiceProvider* service_provider);

  virtual ~DecoderFactory() {}

  // Creates a |Processor| object for decoding a given stream type. Calls back with a
  // decoder if the operation succeeds, with nullptr if not. This method may
  // call back synchronously.
  virtual void CreateDecoder(const StreamType& stream_type,
                             fit::function<void(std::shared_ptr<Processor>)> callback) = 0;

 protected:
  DecoderFactory() {}

 private:
  // Disallow copy and assign.
  DecoderFactory(const DecoderFactory&) = delete;
  DecoderFactory& operator=(const DecoderFactory&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_PROCESS_PROCESSOR_H_
