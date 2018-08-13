// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_FRAMEWORK_REFS_H_
#define GARNET_BIN_MEDIAPLAYER_FRAMEWORK_REFS_H_

#include <cstddef>

namespace media_player {

class Graph;
class StageImpl;
class Input;
class Output;
class Engine;
class InputRef;
class OutputRef;
class GenericNode;

// Opaque Stage pointer used for graph building.
class NodeRef {
 public:
  NodeRef() {}

  NodeRef& operator=(std::nullptr_t) {
    stage_ = nullptr;
    return *this;
  }

  // Returns the number of inputs the node has.
  size_t input_count() const;

  // Returns a reference to the specified input.
  InputRef input(size_t index) const;

  // Returns a reference to the only input. input_count must return 1 for this
  // call to be valid.
  InputRef input() const;

  // Returns the number of outputs the node has.
  size_t output_count() const;

  // Returns a reference to the specified output.
  OutputRef output(size_t index) const;

  // Returns a reference to the only output. output_count must return 1 for this
  // call to be valid.
  OutputRef output() const;

  // Returns true if the reference refers to a node, false if it's null.
  explicit operator bool() const { return stage_ != nullptr; }

  // Gets the actual node referenced by this |NodeRef|.
  GenericNode* GetGenericNode();

  bool operator==(const NodeRef& other) const { return stage_ == other.stage_; }
  bool operator!=(const NodeRef& other) const { return stage_ != other.stage_; }

 private:
  explicit NodeRef(StageImpl* stage) : stage_(stage) {}

  StageImpl* stage_ = nullptr;

  friend Graph;
  friend InputRef;
  friend OutputRef;
  friend Engine;
};

// Opaque Input pointer used for graph building.
class InputRef {
 public:
  InputRef() {}

  InputRef& operator=(std::nullptr_t) {
    input_ = nullptr;
    return *this;
  }

  // Returns true if the reference refers to an input, false if it's null.
  explicit operator bool() const { return input_; }

  // Returns a reference to the node that owns this input. Returns a null
  // reference if this reference is null.
  NodeRef node() const;

  // Indicates whether this input is connected to an output. Calling this method
  // on a null |InputRef| results in undefined behavior.
  bool connected() const;

  // Indicates whether this input is prepared. Calling this method on a null
  // |InputRef| results in undefined behavior.
  bool prepared() const;

  // Returns a reference to the output to which this input is connected. Returns
  // an invalid reference if this input isn't connected to an output.
  OutputRef mate() const;

  bool operator==(const InputRef& other) const {
    return input_ == other.input_;
  }

  bool operator!=(const InputRef& other) const {
    return input_ != other.input_;
  }

 private:
  explicit InputRef(Input* input) : input_(input) {}

  // Returns the actual input referenced by this object.
  Input* actual() const { return input_; }

  Input* input_ = nullptr;

  friend Graph;
  friend NodeRef;
  friend OutputRef;
  friend Output;
  friend Engine;
};

// Opaque Output pointer used for graph building.
class OutputRef {
 public:
  OutputRef() {}

  // Private constructor exposed for testing.
  explicit OutputRef(Output* output) : output_(output) {}

  OutputRef& operator=(std::nullptr_t) {
    output_ = nullptr;
    return *this;
  }

  // Returns true if the reference refers to an output, false if it's null.
  explicit operator bool() const { return output_; }

  // Returns a reference to the node that owns this output. Returns a null
  // reference if this reference is null.
  NodeRef node() const;

  // Indicates whether this output is connected to an input. Calling this method
  // on a null |OutputRef| results in undefined behavior.
  bool connected() const;

  // Returns a reference to the input to which this output is connected. Returns
  // an invalid reference if this output isn't connected to an input.
  InputRef mate() const;

  bool operator==(const OutputRef& other) const {
    return output_ == other.output_;
  }

  bool operator!=(const OutputRef& other) const {
    return output_ != other.output_;
  }

 private:
  // Returns the actual input referenced by this object.
  Output* actual() const { return output_; };

  Output* output_ = nullptr;

  friend Graph;
  friend NodeRef;
  friend InputRef;
  friend Input;
  friend Engine;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_FRAMEWORK_REFS_H_
