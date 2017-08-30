// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_TYPE_CONVERTER_H_
#define LIB_FIDL_CPP_BINDINGS_TYPE_CONVERTER_H_

namespace fidl {

// Specialize the following class:
//   template <typename T, typename U> struct TypeConverter;
// to perform type conversion for Mojom-defined structs and arrays. Here, T is
// the target type; U is the input type.
//
// Specializations should implement the following interfaces:
//   namespace fidl {
//   template <>
//   struct TypeConverter<X, Y> {
//     static X Convert(const Y& input);
//   };
//   template <>
//   struct TypeConverter<Y, X> {
//     static Y Convert(const X& input);
//   };
//   }
//
// EXAMPLE:
//
// Suppose you have the following Mojom-defined struct:
//
//   module geometry {
//   struct Point {
//     int32 x;
//     int32 y;
//   };
//   }
//
// Now, imagine you wanted to write a TypeConverter specialization for
// gfx::Point. It might look like this:
//
//   namespace fidl {
//   template <>
//   struct TypeConverter<geometry::PointPtr, gfx::Point> {
//     static geometry::PointPtr Convert(const gfx::Point& input) {
//       geometry::PointPtr result;
//       result->x = input.x();
//       result->y = input.y();
//       return result;
//     }
//   };
//   template <>
//   struct TypeConverter<gfx::Point, geometry::PointPtr> {
//     static gfx::Point Convert(const geometry::PointPtr& input) {
//       return input ? gfx::Point(input->x, input->y) : gfx::Point();
//     }
//   };
//   }
//
// With the above TypeConverter defined, it is possible to write code like this:
//
//   void AcceptPoint(const geometry::PointPtr& input) {
//     // With an explicit cast using the .To<> method.
//     gfx::Point pt = input.To<gfx::Point>();
//
//     // With an explicit cast using the static From() method.
//     geometry::PointPtr output = geometry::Point::From(pt);
//
//     // Inferring the input type using the ConvertTo helper function.
//     gfx::Point pt2 = ConvertTo<gfx::Point>(input);
//   }
//
template <typename T, typename U>
struct TypeConverter;

// The following specialization is useful when you are converting between
// Array<POD> and std::vector<POD>.
template <typename T>
struct TypeConverter<T, T> {
  static T Convert(const T& obj) { return obj; }
};

// The following helper function is useful for shorthand. The compiler can infer
// the input type, so you can write:
//   OutputType out = ConvertTo<OutputType>(input);
template <typename T, typename U>
inline T ConvertTo(const U& obj) {
  return TypeConverter<T, U>::Convert(obj);
};

}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_TYPE_CONVERTER_H_
