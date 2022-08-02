# Fuchsia Camera Raw Formats Library

This library contains data structures which can be used to represent raw
image formats with arbitrary color filters, bit depths, and packing in
memory.

## Development Notes

This CL relies pretty heavily on C++20 features, in particular all the
complicated constexpr initialization. It would be possible to create a
less ideal version of this library in C++17. The plan is to keep the
usage of this library contained within the camera stack for now.

The code uses new and delete in places in order to be constexpr
(usable at compile time). All such code has constexpr unit tests, where
the compiler will detect any memory leaks or undefined behavior while
running the code at compile time and stop with a compiler error.

## RawFormat

The primary type this library defines is the RawFormat struct. It is in
turn comprised of 3 sub-types.

### ColorFilter

A 2d array of PixelColor values, assumed to be tiled across the image
starting at the top left. This can be used to look up the color value of
any pixel in an image. This is also how things like the "bayer phase"
(eg. RGGB) are defined for RAW formats that contain bayer data.

### BitDepthMap

This goes hand in hand with the ColorFilter. Every color in the
ColorFilter also has an associated bit depth (size in bits for the
pixel). This struct can be used to look up the bit depth of any pixel in
the image. For most standard bayer formats these will all be the same
(eg. 10 bits for every color in RAW10), but this library supports
different sizes for different filter slots in case we encounter that in
the future.

### PackingBlock

This is a recursive data structure (comprised of other PackingBlocks as
well as PixelPieces and Padding) which describes the packing layout of
the sequence of pixels within a buffer, independent of color
information. This data structure can be used to fetch and reconstruct
any pixel in an image buffer.

### Hashing

The RawFormat struct also contains an "id" field which is a hash of the
RawFormat struct's other contents. This means that (barring
extraordinarily unlikely collisions) every format definition which is
physically distinct will have a distinct ID. If the same physical format
is defined twice with two RawFormat struct instances, they will have the
same id and operator== will return true. All the hashing is implemented
by this library because std::hash doesn't support constexpr operations
yet. That also means any users of this library (on the same system and
using the same compiler) should see the same hash value regardless of
process.

### Compile Time Initialization

The RawFormat struct can be constexpr (instantiated at compile time) as
well as created at run time (which uses dynamic allocation). Using
constexpr instances of RawFormat should be preferred whenever possible
as it is better for performance (no dynamic allocation or deep copies to
worry about). For example, if one has dynamically constructed a
RawFormat based on information received at runtime (eg. via a FIDL call
or network message), one can use the GetFormatById function from
raw_formats.h to see if the ID matches a format that has a constexpr
definition. If so the dynamically allocated RawFormat can be immediately
destroyed and the constexpr version used instead.

## RawFormatInstance

For the sake of performance when doing repeated getting/setting of pixel
values from a buffer, the GetPixel/SetPixel functions take a
"RawFormatInstance" struct. A RawFormatInstance is created from a
RawFormat and a specific image size (width, height, and optionally
stride) via the CreateFormatInstance function. A number of computations
based on the image size which would have to be performed on each lookup
are memoized in the RawFormatInstance's PackingBlock.

The RawFormatInstance struct has an "id" field and an "rid" field. The
rid is the id of the RawFormat from which this RawFormatInstance was
created. The "id" field is hash of the rid, width, height, and row
stride if it is present. The "id" field is used by operator==.
