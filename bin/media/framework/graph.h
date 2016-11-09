// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>

#include "apps/media/src/framework/engine.h"
#include "apps/media/src/framework/refs.h"
#include "apps/media/src/framework/stages/active_multistream_source_stage.h"
#include "apps/media/src/framework/stages/active_sink_stage.h"
#include "apps/media/src/framework/stages/active_source_stage.h"
#include "apps/media/src/framework/stages/multistream_source_stage.h"
#include "apps/media/src/framework/stages/stage.h"
#include "apps/media/src/framework/stages/transform_stage.h"

namespace media {

namespace {

// StageCreator::Create creates a stage for a part. DEFINE_STAGE_CREATOR defines
// a specialization for a particular model/stage type pair. Every new
// model/stage type pair that's defined will need an entry here.
template <typename T, typename Enable = void>
class StageCreator;

#define DEFINE_STAGE_CREATOR(TModel, TStage)                                 \
  template <typename T>                                                      \
  class StageCreator<                                                        \
      T, typename std::enable_if<std::is_base_of<TModel, T>::value>::type> { \
   public:                                                                   \
    static inline Stage* Create(std::shared_ptr<T> t_ptr) {                  \
      return new TStage(std::shared_ptr<TModel>(t_ptr));                     \
    }                                                                        \
  };

DEFINE_STAGE_CREATOR(MultistreamSource, MultistreamSourceStage);
DEFINE_STAGE_CREATOR(Transform, TransformStage);
DEFINE_STAGE_CREATOR(ActiveSource, ActiveSourceStage);
DEFINE_STAGE_CREATOR(ActiveSink, ActiveSinkStage);
DEFINE_STAGE_CREATOR(ActiveMultistreamSource, ActiveMultistreamSourceStage);

#undef DEFINE_STAGE_CREATOR

}  // namespace

//
// USAGE
//
// Graph is a container for sources, sinks and transforms ('parts') connected
// in a graph. PartRef, InputRef and OutputRef are all
// references to parts and their inputs and outputs. Graph provides a variety
// of methods for adding and removing parts and for connecting inputs and
// outputs to form a graph.
//
// The graph isn't thread-safe. If the graph is to be modified and/or
// interrogated on multiple threads, the caller must provide its own lock
// to prevent collisions. In this case, the caller must also acquire the same
// lock when making calls that cause parts to add or remove inputs or outputs.
//
// The graph prevents the disconnection of prepared inputs and outputs. Once
// a connected input/output pair is prepared, it must be unprepared before
// disconnection. This allows the engine to operate freely over prepared
// portions of the graph (prepare and unprepare are synchronized with the
// engine).
//
// Parts added to the graph are referenced using shared pointers. The graph
// holds pointers to the parts it contains, and the application, in many cases,
// also holds pointers to the parts so it can call methods that are outside the
// graph's scope. When a part is added, the graph returns a PartRef
// object, which can be used to reference the part when the graph is modified.
// PartRef objects can be interrogated to retrieve inputs (as
// InputRef objects) and outputs (as OutputRef objects).
//
// Parts come in various flavors, defined by 'model' abstract classes. The
// current list of supported models is:
//
//  ActiveSink        - a sink that consumes packets asynchronously
//  ActiveSource      - a source that produces packets asynchronously
//  MultistreamSource - a source that produces multiple streams of packets
//                      synchronously
//  Transform         - a synchronous transform that consumes and produces
//                      packets via one input and one output
//
// Other models will be defined in the future as needed.
//

//
// DESIGN
//
// The Graph is implemented as a system of cooperating objects. Of those
// objects, only the graph itself is of relevance to code that uses Graph and
// to part implementations. The other objects are:
//
// Stage
// A stage hosts a single part. There are many subclasses of Stage, one for
// each supported part model. The stage's job is to implement the contract
// represented by the model so the parts that conform to the model can
// participate in the operation of the graph. Stages are uniform with respect
// to how they interact with graph. PartRef references a stage.
//
// Input
// A stage possesses zero or more Input instances. Input objects
// implement the supply of media into the stage and demand for media signalled
// upstream. Inputs recieve media from Outputs in the form of packets
// (type Packet).
//
// Output
// A stage possesses zero or more Output instances. Output objects
// implement the supply of media output of the stage to a downstream input and
// demand for media signalled from that input.
//

// Host for a source, sink or transform.
class Graph {
 public:
  Graph();

  ~Graph();

  // Adds a part to the graph.
  template <typename T>
  PartRef Add(std::shared_ptr<T> t_ptr) {
    FTL_DCHECK(t_ptr);
    return Add(StageCreator<T>::Create(t_ptr));
  }

  // Removes a part from the graph after disconnecting it from other parts.
  void RemovePart(PartRef part);

  // Connects an output connector to an input connector. Returns the dowstream
  // part.
  PartRef Connect(const OutputRef& output, const InputRef& input);

  // Connects a part with exactly one output to a part with exactly one input.
  // Returns the downstream part.
  PartRef ConnectParts(PartRef upstream_part, PartRef downstream_part);

  // Connects an output connector to a part that has exactly one input. Returns
  // the downstream part.
  PartRef ConnectOutputToPart(const OutputRef& output, PartRef downstream_part);

  // Connects a part with exactly one output to an input connector. Returns the
  // downstream part.
  PartRef ConnectPartToInput(PartRef upstream_part, const InputRef& input);

  // Disconnects an output connector and the input connector to which it's
  // connected.
  void DisconnectOutput(const OutputRef& output);

  // Disconnects an input connector and the output connector to which it's
  // connected.
  void DisconnectInput(const InputRef& input);

  // Disconnects and removes part and everything connected to it.
  void RemovePartsConnectedToPart(PartRef part);

  // Disconnects and removes everything connected to output.
  void RemovePartsConnectedToOutput(const OutputRef& output);

  // Disconnects and removes everything connected to input.
  void RemovePartsConnectedToInput(const InputRef& input);

  // Adds all the parts in t (which must all have one input and one output) and
  // connects them in sequence to the output connector. Returns the output
  // connector of the last part or the output parameter if it is empty.
  template <typename T>
  OutputRef AddAndConnectAll(OutputRef output, const T& t) {
    for (const auto& element : t) {
      PartRef part = Add(StageCreator<T>::Create(element));
      Connect(output, part.input());
      output = part.output();
    }
    return output;
  }

  // Removes all parts from the graph.
  void Reset();

  // Prepares the graph for operation.
  void Prepare();

  // Prepares the input and everything upstream of it. This method is used to
  // prepare subgraphs added when the rest of the graph is already prepared.
  void PrepareInput(const InputRef& input);

  // Flushes the output and the subgraph downstream of it.
  void FlushOutput(const OutputRef& output);

  // Flushes the output and the subgraph downstream of it.
  void FlushAllOutputs(PartRef part);

 private:
  // Adds a stage to the graph.
  PartRef Add(Stage* stage);

  std::list<Stage*> stages_;
  std::list<Stage*> sources_;
  std::list<Stage*> sinks_;

  Engine engine_;
  Stage::UpdateCallback update_function_;
};

}  // namespace media
