// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/lib/numerics/rational.h"

#include <numeric>

namespace camera::numerics {

Rational& Rational::Reduce() {
  *this = camera::numerics::Reduce(*this);
  return *this;
}

Rational Reduce(const Rational& r) {
  auto ret = r;
  auto s = std::gcd(ret.n, ret.d);
  if (s > 1) {
    ret.n /= s;
    ret.d /= s;
  }
  if (ret.d < 0) {
    ret.n = -ret.n;
    ret.d = -ret.d;
  }
  return ret;
}

Rational& Rational::operator+=(const Rational& other) {
  *this = *this + other;
  return *this;
}

Rational& Rational::operator-=(const Rational& other) {
  *this = *this - other;
  return *this;
}

Rational& Rational::operator*=(const Rational& other) {
  *this = *this * other;
  return *this;
}

Rational& Rational::operator/=(const Rational& other) {
  *this = *this / other;
  return *this;
}

Rational operator+(const Rational& r) { return r; }

Rational operator-(const Rational& r) { return Reduce({.n = -r.n, .d = r.d}); }

Rational operator+(const Rational& a, const Rational& b) {
  return Reduce({.n = a.n * b.d + b.n * a.d, .d = a.d * b.d});
}

Rational operator-(const Rational& a, const Rational& b) { return Reduce(a + (-b)); }

Rational operator*(const Rational& a, const Rational& b) {
  return Reduce({.n = a.n * b.n, .d = a.d * b.d});
}

Rational operator/(const Rational& a, const Rational& b) {
  return Reduce({.n = a.n * b.d, .d = a.d * b.n});
}

bool operator==(const Rational& a, const Rational& b) { return Reduce(a - b).n == 0; }

bool operator!=(const Rational& a, const Rational& b) { return Reduce(a - b).n != 0; }

bool operator<(const Rational& a, const Rational& b) { return Reduce(a - b).n < 0; }

bool operator<=(const Rational& a, const Rational& b) { return Reduce(a - b).n <= 0; }

bool operator>(const Rational& a, const Rational& b) { return Reduce(a - b).n > 0; }

bool operator>=(const Rational& a, const Rational& b) { return Reduce(a - b).n >= 0; }

std::ostream& operator<<(std::ostream& os, const Rational& r) {
  constexpr auto kFractionSlash = u8"\u2044";
  os << r.n << kFractionSlash << r.d;
  return os;
}

}  // namespace camera::numerics
