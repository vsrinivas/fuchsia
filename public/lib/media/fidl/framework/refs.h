// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FRAMEWORK_REFS_H_
#define SERVICES_MEDIA_FRAMEWORK_REFS_H_

#include <stdint.h>

namespace mojo {
namespace media {

class Graph;
class Stage;
class Input;
class Output;
class Engine;
class InputRef;
class OutputRef;

// Opaque Stage pointer used for graph building.
class PartRef {
 public:
  PartRef() : stage_(nullptr) {}

  PartRef& operator=(std::nullptr_t) {
    stage_ = nullptr;
    return *this;
  }

  // Returns the number of inputs the part has.
  size_t input_count() const;

  // Returns a reference to the specified input.
  InputRef input(size_t index) const;

  // Returns a reference to the only input. input_count must return 1 for this
  // call to be valid.
  InputRef input() const;

  // Returns the number of outputs the part has.
  size_t output_count() const;

  // Returns a reference to the specified output.
  OutputRef output(size_t index) const;

  // Returns a reference to the only output. output_count must return 1 for this
  // call to be valid.
  OutputRef output() const;

  // Returns true if the reference refers to a part, false if it's null.
  explicit operator bool() const { return stage_ != nullptr; }

 private:
  explicit PartRef(Stage* stage) : stage_(stage) {}

  // Determines if the reference is non-null and otherwise valid. Useful for
  // DCHECKs.
  bool valid() const { return stage_ != nullptr; }

  Stage* stage_;

  friend Graph;
  friend InputRef;
  friend OutputRef;
  friend Engine;
};

// Opaque Input pointer used for graph building.
class InputRef {
 public:
  InputRef() : stage_(nullptr), index_(0) {}

  InputRef& operator=(std::nullptr_t) {
    stage_ = nullptr;
    index_ = 0;
    return *this;
  }

  // Returns true if the reference refers to an input, false if it's null.
  explicit operator bool() const { return stage_ != nullptr; }

  // Returns a reference to the part that owns this input. Returns a null
  // reference if this reference is null.
  PartRef part() const { return PartRef(stage_); }

  // Indicates whether this input is connected to an output.
  bool connected() const;

  // Returns a reference to the output to which this input is connected. Returns
  // an invalid reference if this input isn't connected to an output.
  const OutputRef& mate() const;

 private:
  InputRef(Stage* stage, size_t index);

  // Returns the actual input referenced by this object.
  Input& actual() const;

  // Determines if the reference is non-null and otherwise valid. Useful for
  // DCHECKs.
  bool valid() const;

  Stage* stage_;
  size_t index_;

  friend Graph;
  friend PartRef;
  friend OutputRef;
  friend Output;
  friend Engine;
};

// Opaque Output pointer used for graph building.
class OutputRef {
 public:
  OutputRef() : stage_(nullptr), index_(0) {}

  OutputRef& operator=(std::nullptr_t) {
    stage_ = nullptr;
    index_ = 0;
    return *this;
  }

  // Returns true if the reference refers to an output, false if it's null.
  explicit operator bool() const { return stage_ != nullptr; }

  // Returns a reference to the part that owns this output. Returns a null
  // reference if this reference is null.
  PartRef part() const { return PartRef(stage_); }

  // Indicates whether this output is connected to an input.
  bool connected() const;

  // Returns a reference to the input to which this output is connected. Returns
  // an invalid reference if this output isn't connected to an input.
  const InputRef& mate() const;

 private:
  OutputRef(Stage* stage, size_t index);

  // Returns the actual input referenced by this object.
  Output& actual() const;

  // Determines if the reference is non-null and otherwise valid. Useful for
  // DCHECKs.
  bool valid() const;

  Stage* stage_;
  size_t index_;

  friend Graph;
  friend PartRef;
  friend InputRef;
  friend Input;
  friend Engine;
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FRAMEWORK_REFS_H_
