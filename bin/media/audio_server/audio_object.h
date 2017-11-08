// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>

namespace media {
namespace audio {

// An audio object is the simple base class for 4 major types of audio objects
// in the mixer; Outputs, Inputs, Renderers and Capturers.  It ensures that each
// of these objects is intrusively ref-counted, and remembers its type so that
// it may be safely downcast from a generic audio object to something more
// specific.
//
// TODO(johngro) : Refactor AudioRenderers so that they derive from AudioObject.
class AudioObject : public fbl::RefCounted<AudioObject> {
 public:
  enum class Type {
    Output,
    Input,
    Renderer,
    Capturer,
  };

  Type type() const { return type_; }

 protected:
  friend class fbl::RefPtr<AudioObject>;
  explicit AudioObject(Type type) : type_(type) {}
  virtual ~AudioObject() {}

 private:
  const Type type_;
};

}  // namespace audio
}  // namespace media
