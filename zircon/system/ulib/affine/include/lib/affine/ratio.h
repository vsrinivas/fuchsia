// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_AFFINE_RATIO_H_
#define LIB_AFFINE_RATIO_H_

#include <stdint.h>
#include <limits>
#include <type_traits>
#include <zircon/assert.h>

namespace affine {

class Transform;    // fwd decl for friendship

class Ratio {
public:
    enum class Exact { No, Yes };

    // Used to indicate overflow/underflow of scaling operations.
    static constexpr int64_t kOverflow = std::numeric_limits<int64_t>::max();
    static constexpr int64_t kUnderflow = std::numeric_limits<int64_t>::min();

    // Reduces the ratio of N/D
    //
    // Defined only for uint32_t and uint64_t
    template <typename T>
        static void Reduce(T* numerator, T* denominator);

    // Produces the product two 32 bit ratios. If exact is true, ASSERTs on loss
    // of precision.
    static void Product(uint32_t a_numerator,
                        uint32_t a_denominator,
                        uint32_t b_numerator,
                        uint32_t b_denominator,
                        uint32_t* product_numerator,
                        uint32_t* product_denominator,
                        Exact exact = Exact::Yes);

    // Produces the product of a 32 bit ratio and the int64_t as an int64_t. Returns
    // a saturated value (either kOverflow or kUnderflow) on overflow/underflow.
    static int64_t Scale(int64_t value, uint32_t numerator, uint32_t denominator);

    // Returns the product of the ratios. If exact is true, ASSERTs on loss of
    // precision.
    static Ratio Product(Ratio a, Ratio b, Exact exact = Exact::Yes) {
        uint32_t result_numerator;
        uint32_t result_denominator;
        Product(a.numerator(), a.denominator(),
                b.numerator(), b.denominator(),
                &result_numerator, &result_denominator,
                exact);
        return Ratio(result_numerator, result_denominator, NoReduce::Tag);
    }

    Ratio() = default;
    Ratio(uint32_t numerator, uint32_t denominator)
        : numerator_(numerator), denominator_(denominator) {
        Reduce(&numerator_, &denominator_);
    }

    uint32_t numerator() const { return numerator_; }
    uint32_t denominator() const { return denominator_; }
    bool invertible() const { return numerator_ != 0; }

    Ratio Inverse() const {
        ZX_ASSERT(invertible());
        return Ratio{denominator_, numerator_, NoReduce::Tag};
    }

    int64_t Scale(int64_t value) const {
        return Scale(value, numerator_, denominator_);
    }

private:
    friend class Transform;
    enum class NoReduce { Tag };

    Ratio(uint32_t numerator, uint32_t denominator, NoReduce)
        : numerator_(numerator), denominator_(denominator) {}

    uint32_t numerator_ = 1;
    uint32_t denominator_ = 1;
};

// Tests two ratios for equality.
inline bool operator==(Ratio a, Ratio b) {
  return a.numerator() == b.numerator() &&
         a.denominator() == b.denominator();
}

// Tests two ratios for inequality.
inline bool operator!=(Ratio a, Ratio b) { return !(a == b); }

// Returns the ratio of the two ratios.
inline Ratio operator/(Ratio a, Ratio b) {
  return Ratio::Product(a, b.Inverse());
}

// Returns the product of the two ratios.
inline Ratio operator*(Ratio a, Ratio b) {
  return Ratio::Product(a, b);
}

// Returns the product of the rate and the int64_t.
inline int64_t operator*(Ratio a, int64_t b) { return a.Scale(b); }

// Returns the product of the rate and the int64_t.
inline int64_t operator*(int64_t a, Ratio b) { return b.Scale(a); }

// Returns the the int64_t divided by the rate.
inline int64_t operator/(int64_t a, Ratio b) {
  return b.Inverse().Scale(a);
}

}  // namespace affine

#endif  // LIB_AFFINE_RATIO_H_
