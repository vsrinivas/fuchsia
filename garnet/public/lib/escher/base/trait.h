// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_BASE_TRAIT_H_
#define LIB_ESCHER_BASE_TRAIT_H_

namespace escher {

// This class serves to document a C++ design pattern which extends the idea of
// a pure interface by adding non-virtual methods which are defined in terms of
// pure virtual methods that subclasses are required to implement.
//
// For example, a RectangleTrait adds the ability to compute an area to any
// class that can provide a width and a height:
//
// class RectangleTrait {
//  public:
//   float area() const { return width() * height(); }
//
//  private:
//   virtual float width() const = 0;
//   virtual float height() const = 0;
// };
//
// The key feature of this design pattern is its statelessness (i.e. the trait
// class has no instance variables), and is therefore safe to use with multiple
// inheritance, for the same reasons it is safe for a class to implement several
// pure virtual interfaces.
//
// NOTE: Being unaware of an idiomatic name for this C++ pattern, we take the
// name from an analogous language feature first introduced in Squeak, and soon
// popularized in Scala: http://scg.unibe.ch/research/traits.
class Trait {
 protected:
  virtual ~Trait() = default;
};

}  // namespace escher

#endif  // LIB_ESCHER_BASE_TRAIT_H_
