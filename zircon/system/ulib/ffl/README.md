# Fuchsia Fixed-Point Library (FFL)

## Introduction

FFL is a C++ template library for fixed-point arithmetic. The library is
primarily intended to support the Zircon kernel scheduler however, it is
sufficiently general to be useful wherever fixed-point computations are
needed.

FFL is motivated by the following requirements:
* Availability: Fixed-point support is not yet ratified in the standard library.
  We need a solution today.
* Dependency: Many alternatives require additional dependencies. We prefer to
  depend only on the standard library.
* Rounding: Many alternatives, including the proposal for the standard library,
  have poor or ill-defined rounding behavior. We need well-defined rounding with
  reasonable general-purpose stability, such as convergent rounding.

## General Usage

The main user-facing type in FFL is the value template type
`ffl::Fixed<typename Integer, size_t FractionalBits>`. This template accepts an
integer type for the underlying value and the number of bit to use to represent
the fractional component. Naturally, the range of the integer component of the
fixed-point value is defined by the difference between the number of bits of the
underlying integer type and the number of bits reserved for the fractional part.

`ffl::Fixed` behaves similarly to plain integers. The type supports most of the
same arithmetic operators: addition, subtraction, negation, multiplication, and
division, as well as all of the comparison operators.

```C++
#include <ffl/fixed.h>

using ffl::Fixed;

Fixed<int32_t, 31> UnitaryRatio(Fixed<int32_t, 0> a, Fixed<int32_t, 0> b) {
    if (a > b)
        return b / a;
    else
        return a / b;
}

Fixed<uint8_t, 0> Blend(Fixed<uint8_t, 0> color0, Fixed<uint8_t, 0> color1, Fixed<uint8_t, 8> alpha) {
    return alpha * color0 + (Fixed<uint8_t, 8>{1} - alpha) * color1;
}
```

## Expressions and Managing Precision

FFL supports arithmetic and comparisons with mixed precisions. These operations
may consider both the source and destination precisions at compile time to
select an appropriate strategy for intermediate computations.

In order to consider the destination precision, the evaluation of an expression
involving `ffl::Fixed` values is deferred until the expression is assigned to
a `ffl::Fixed` variable. To facilitate this deferred evaluation, the arithmetic
operators return instances of the template type `ffl::Expression`, which
captures the arithmetic operation and the arguments involved. The arguments may
be instances of `ffl::Fixed`, plain integers, or other instances of
`ffl::Expression` returned by other operators and utility functions. With this
approach, compound expressions result in expression trees that follow the C++
order of operations.

In many cases the use of expression trees is transparent to the user, as in the
following example:

```C++
#include <ffl/fixed.h>

using ffl::Fixed;

struct Point2d {
    Fixed<int16_t, 0> x;
    Fixed<int16_t, 0> y;
};

Point2d LinearInterpolate(Point2d p0, Point2d p1, Fixed<int32_t, 16> t) {
    return {p0.x + t * (p1.x - p0.x), p0.y + t * (p1.y - p0.y)};
}
```

In this example the arithmetic expressions and assignments happen together and
there is no need to consider the intermediate Expression objects.

In some cases it is useful to be aware of the intermediate Expression objects.
The following example uses intermediate expressions to make the overall
computation more readable:

```C++
#include <ffl/fixed.h>

using ffl::Fixed;

// TODO
```

### Mixed Precision

FFL uses the following rules when performing mixed precision arithmetic:
* Addition and subtraction convert to the greatest precision and least
  resolution of the two operands before computing an intermediate result.
* Multiplication produces an intermediate result with precision and resolution
  sufficient to hold the sum of both the fractional and integral bit depths of the
  operands.
* Division produces an intermediate result with the resolution of the target
  format.

Comparisons convert to the least resolution of the two operands before
performing the comparison. However, when comparing a fixed-point value with a
plain integer, the values are converted to an intermediate type with sufficient
precision and the resolution of the fixed-point argument.

### Intermediate Values and Saturation

Saturation is one important difference between fixed-point arithmetic in FFL and
regular integer arithmetic. Regular integer arithmetic over or underflows when
the result exceeds the range of the integral type. In contrast, FFL uses
intermediate values with sufficient range for the computation. When an
intermediate value is finally assigned to a fixed-point variable the value is
clamped to precision of the destination type.

### Coercing Resolution

Some arithmetic operations take the target resolution into account when
computing intermediate values (only division at the time of this writing). The
target resolution may be influenced using the `ToResolution<FractionalBits>()`
utility function. This utility functions by inserting a resolution node into the
expression tree at the point of invocation; deeper nodes that consider target
resolution will consider the resolution given by this node instead of the final
resolution.

```C++
#include <ffl/fixed.h>

using ffl::Fixed;
using ffl::Round;
using ffl::ToResolution;

constexpr int32_t Divide(int32_t numerator, int32_t denominator) {
    const Fixed<int32_t, 0> fixed_numerator{numerator};
    const Fixed<int32_t, 0> fixed_denominator{denominator};

    // Perform division with 2bit fractional resolution for optimum convergent
    // rounding of the quotient.
    return Round<int32_t>(ToResolution<2>(fixed_numerator / fixed_denominator));
}
```

