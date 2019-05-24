// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_AFFINE_TRANSFORM_H_
#define LIB_AFFINE_TRANSFORM_H_

#include <lib/affine/ratio.h>
#include <safemath/clamped_math.h>

namespace affine {

// A small helper class which represents a 1 dimensional affine transformation
// from a signed 64 bit space A, to a signed 64 bit space B.  Conceptually, this
// is the function...
//
// f(a) = b = (a * scale) + offset
//
// Internally, however, the exact function used is
//
// f(a) = b = (((a - A_offset) * B_scale) / A_scale) + B_offset
//
// Where the offsets involved are 64 bit signed integers, and the scale factors
// are 32 bit unsigned integers.
//
// Overflow/Underflow saturation behavior is as follows.
// The transformation operation is divided into three stages.
//
// 1) Offset by A_offset
// 2) Scale by (B_scale / A_scale)
// 3) Offset by B_offset
//
// Each stage is saturated indepenedently.  That is to say, if the result of
// stage #1 is clamped at int64::min, this is the input value which will be fed
// into stage #2.  The calculations are *not* done with infinite precision and
// then clamped at the end.
//
// TODO(johngro): Reconsider this.  Clamping at intermediate stages can make it
// more difficult to understand that saturation happened at all, and might be
// important to a client.  It may be better to either signal explicitly that
// this happened, or to extend the precision of the operation in the rare slow
// path so that saturation behavior occurs only at the end of the op, and
// produces a correct result if the transform would have saturated at an
// intermediate step, but got brought back into range by a subsequent operation.
//
// Saturation is enabled by default, but may be disabled by choosing the
// Saturate::No form of Apply/ApplyInverse.  When saturation behavior is
// disabled, the results of a transformation where over/underflow occurs at any
// stage is undefined.
//
class Transform {
public:
    using Exact = Ratio::Exact;
    enum class Saturate { No, Yes };

    // Applies a transformation from A -> B
    template <Saturate SATURATE = Saturate::Yes>
    static int64_t Apply(int64_t a_offset,
                         int64_t b_offset,
                         Ratio ratio,  // Ratio of B_scale:A_scale
                         int64_t val) {
        if constexpr (SATURATE == Saturate::Yes) {
            return safemath::ClampAdd(ratio.Scale(safemath::ClampSub(val, a_offset)), b_offset);
        } else {
            // TODO(johngro) : the multiplication by the ratio operation here
            // actually implements saturation behavior.  If we want this
            // operation to actually perform no saturation checks at all, we
            // need to make a Saturate::No version of Ratio::Scale.
            return ((val - a_offset) * ratio) + b_offset;
        }
    }

    // Applies the inverse transformation B -> A
    template <Saturate SATURATE = Saturate::Yes>
    static int64_t ApplyInverse(int64_t a_offset,
                                int64_t b_offset,
                                Ratio ratio,  // Ratio of B_scale:A_scale
                                int64_t val) {
        return Apply<SATURATE>(b_offset, a_offset, ratio.Inverse(), val);
    }

    // Default construction is identity
    Transform() = default;

    // Explicit construction
    Transform(int64_t a_offset,
              int64_t b_offset,
              Ratio ratio) : a_offset_(a_offset), b_offset_(b_offset), ratio_(ratio) {}

    // Construct a linear transformation (zero offsets) from a ratio
    explicit Transform(Ratio ratio)
        : a_offset_(0), b_offset_(0), ratio_(ratio) {}

    bool     invertible()  const { return ratio_.invertible(); }
    int64_t  a_offset()    const { return a_offset_; }
    int64_t  b_offset()    const { return b_offset_; }
    Ratio    ratio()       const { return ratio_; }
    uint32_t numerator()   const { return ratio_.numerator(); }
    uint32_t denominator() const { return ratio_.denominator(); }

    // Construct and return a transform which is the inverse of this transform.
    Transform Inverse() const {
        return Transform(b_offset_, a_offset_, ratio_.Inverse());
    }

    // Applies the transformation
    template <Saturate SATURATE = Saturate::Yes>
    int64_t Apply(int64_t val) const {
        return Apply<SATURATE>(a_offset_, b_offset_, ratio_, val);
    }

    // Applies the inverse transformation
    template <Saturate SATURATE = Saturate::Yes>
    int64_t ApplyInverse(int64_t subject_input) const {
        ZX_DEBUG_ASSERT(ratio_.denominator() != 0u);
        return ApplyInverse<SATURATE>(a_offset_, b_offset_, ratio_, subject_input);
    }

    // Applies the transformation using functor operator notation.
    template <Saturate SATURATE = Saturate::Yes>
    int64_t operator()(int64_t val) const {
        return Apply<SATURATE>(val);
    }

    // Composes two timeline functions B->C and A->B producing A->C. If exact is
    // Exact::Yes, DCHECKs on loss of precision.
    //
    // During composition, the saturation behavior is as follows
    //
    // 1) The intermediate offset (bc.a_offset - ab.b_offset) will be saturated
    //    before distribution to the offsets ac.
    // 2) Both offsets of ac will be saturated as ab.a_offset and bc.b_offset
    //    are combined with the distributed intermediate offset.
    //
    static Transform Compose(const Transform& bc, const Transform& ab, Exact exact = Exact::Yes);

private:
    int64_t a_offset_ = 0;
    int64_t b_offset_ = 0;
    Ratio ratio_{1, 1, Ratio::NoReduce::Tag};
};

// Tests two transforms for equality.
inline bool operator==(const Transform& a, const Transform& b) {
    return (a.a_offset() == b.a_offset()) &&
           (a.b_offset() == b.b_offset()) &&
           (a.ratio() == b.ratio());
}

// Tests two transforms for inequality.
inline bool operator!=(const Transform& a, const Transform& b) {
    return !(a == b);
}

// Composes two timeline functions B->C and A->B producing A->C. DCHECKs on
// loss of precision.
inline Transform operator*(const Transform& bc, const Transform& ab) {
    return Transform::Compose(bc, ab);
}

}  // namespace affine

#endif  // LIB_AFFINE_TRANSFORM_H_
