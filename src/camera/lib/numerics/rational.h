// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_NUMERICS_RATIONAL_H_
#define SRC_CAMERA_LIB_NUMERICS_RATIONAL_H_

#include <compare>
#include <cstdint>
#include <ostream>

namespace camera::numerics {

// Rational represents a rational number. All operations attempt to leave the number as a reduced
// fraction, but no attempt is made to detect or avoid overflow, division by zero, or other
// undefined behaviors.
struct Rational {
  int64_t n = 0;
  int64_t d = 1;
  // Transforms the number into a reduced fraction and ensures the denominator is non-negative.
  Rational& Reduce();
  Rational& operator+=(const Rational& r);
  Rational& operator-=(const Rational& r);
  Rational& operator*=(const Rational& r);
  Rational& operator/=(const Rational& r);
  friend std::ostream& operator<<(std::ostream& os, const Rational& r);
};

Rational Reduce(const Rational& r);
Rational operator+(const Rational& r);
Rational operator-(const Rational& r);
Rational operator+(const Rational& a, const Rational& b);
Rational operator-(const Rational& a, const Rational& b);
Rational operator*(const Rational& a, const Rational& b);
Rational operator/(const Rational& a, const Rational& b);
bool operator==(const Rational& a, const Rational& b);
bool operator!=(const Rational& a, const Rational& b);
bool operator<(const Rational& a, const Rational& b);
bool operator<=(const Rational& a, const Rational& b);
bool operator>(const Rational& a, const Rational& b);
bool operator>=(const Rational& a, const Rational& b);

}  // namespace camera::numerics

#endif  // SRC_CAMERA_LIB_NUMERICS_RATIONAL_H_
