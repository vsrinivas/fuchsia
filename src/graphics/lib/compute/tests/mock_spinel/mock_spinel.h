// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_MOCK_SPINEL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_MOCK_SPINEL_H_

#include <map>
#include <vector>

#include "spinel_api_interface.h"
#include "tests/common/utils.h"

// A simple mock spinel API implementation, useful for testing or sub-classing.
namespace mock_spinel {

//
// Paths
//

// A class modelling paths built from a Spinel path builder.
//
// Each path is a series of elements, where each element has a unique type tag
// and a fixed number of coordinates (depending on its type).
//
// For ease of debugging, all data is stored in a simple vector of floats,
// with special values used as element tags. Another benefit is that the
// GoogleMock ElementAreArray() will give sensible output when differences
// exist. Convenience types and methods are provided to simplify usage,
// i.e.:
//
//   - Element struct types like Path::MoveTo, Path::LineTo, etc.. provide
//     a way to describe or view a given element, with Path::Element being
//     the union type that covers all of them.
//
//   - Use add() to add one element. For example:
//
//        path.add(Path::MoveTo{ .x = 0, .y = 0 });
//        path.add(Path::LineTo{ .x = 10, .y = 10 });
//
//   - Scan all elements of a given path with:
//
//        for (const Path::Element& element : path) {
//           switch (element.tag) {
//             case Path::MoveTo::kTag:   // a MoveTo element
//                auto& move_to = element.move_to;
//                printf("move to %g,%g\n", move_to.x, move_to.y);
//                break;
//             ...
//           }
//        }
//
//   - Write float arrays that contain path elements using the
//     MOCK_SPINEL_PATH_<TYPE>_LITERAL() macros (see example below).
//
struct Path
{
  // Each path is modeled as a series of elements.
  // Each element has a unique type tag value, and a fixed-number of
  // coordinates.
  struct ElementAny
  {
    float tag;
    float coords[8];
  };

  // All elements are encoded serially into a vector of flots.
  // The tag value determines how many floats are used per element.
  std::vector<float> data;

  // All element tags start at kTagBase. This number should be easy
  // to spot when looking at the |data| array directly, e.g. under a debugger,
  // i.e. it should be an unlikely coordinate that stands out.
  static constexpr float kTagBase = 77770;

  // Helper structs to better describe each element.
  // Once can cast an Element* to one of these to read coordinates.
  struct MoveTo
  {
    static constexpr float kTag = kTagBase + 0;
    float                  tag  = kTag;
    float                  x, y;
  };

  struct LineTo
  {
    static constexpr float kTag = kTagBase + 1;
    float                  tag  = kTag;
    float                  x, y;
  };

  struct QuadTo
  {
    static constexpr float kTag = kTagBase + 2;
    float                  tag  = kTag;
    float                  cx, cy, x, y;
  };

  struct CubicTo
  {
    static constexpr float kTag = kTagBase + 3;
    float                  tag  = kTag;
    float                  c1x, c1y, c2x, c2y, x, y;
  };

  struct RatQuadTo
  {
    static constexpr float kTag = kTagBase + 4;
    float                  tag  = kTag;
    float                  cx, cy, x, y, w;
  };

  struct RatCubicTo
  {
    static constexpr float kTag = kTagBase + 5;
    float                  tag  = kTag;
    float                  c1x, c1y, c2x, c2y, x, y, w1, w2;
  };

  // Convert an element tag value to the number of element coordinates.
  static constexpr uint32_t
  tagToCount(float tag)
  {
    if (tag == MoveTo::kTag)
      return 2;
    if (tag == LineTo::kTag)
      return 2;
    if (tag == QuadTo::kTag)
      return 4;
    if (tag == CubicTo::kTag)
      return 6;
    if (tag == RatQuadTo::kTag)
      return 5;
    if (tag == RatCubicTo::kTag)
      return 8;
    return 0;
  }

  union Element
  {
    ElementAny any;
    MoveTo     move_to;
    LineTo     line_to;
    QuadTo     quad_to;
    CubicTo    cubic_to;
    RatQuadTo  ratquad_to;
    RatCubicTo ratcubic_to;

    // Return the number of coordinates for this element.
    uint32_t
    count() const
    {
      return tagToCount(any.tag);
    }

    // Return the size in floats of this element.
    size_t
    sizeInFloats() const
    {
      return (1 + count());
    }
  };

  static_assert(sizeof(Element) == sizeof(ElementAny), "incorrect element size!");
  static_assert(alignof(Element) == 4u, "Incorrect alignment!");

  // Handy constant iterator type to scan all elements in a path.
  // Together with Path::begin() and Path::end(), this allows loops like:
  //
  //    for (const Element& element : paths) {
  //      ...
  //    }
  //
  struct Iterator
  {
    // Pointer dereference
    const Element *
    operator->() const
    {
      return floatPtrToElement(ptr);
    }

    // derefence
    const Element &
    operator*() const
    {
      return *floatPtrToElement(ptr);
    }

    // pre-increment
    Iterator &
    operator++()
    {
      ptr += (*this)->sizeInFloats();
      return *this;
    }

    // post-increment
    Iterator
    operator++(int)
    {
      Iterator result{ ptr };
      ++(*this);
      return result;
    }

    bool
    operator==(const Iterator & other)
    {
      return ptr == other.ptr;
    }

    bool
    operator!=(const Iterator & other)
    {
      return ptr != other.ptr;
    }

    const float * ptr;
  };

  // begin() and end() methods to support range-based loops.
  Iterator
  begin() const
  {
    return { &data[0] };
  }
  Iterator
  end() const
  {
    return { &data[data.size()] };
  }

 protected:
  template <typename FROM, typename TO>
  static constexpr TO *
  pointerCast(FROM * ptr)
  {
    union
    {
      FROM * from;
      TO *   to;
    } u;
    u.from = ptr;
    return u.to;
  }

  static constexpr Element *
  floatPtrToElement(float * ptr)
  {
    return pointerCast<float, Element>(ptr);
  }

  static constexpr const Element *
  floatPtrToElement(const float * ptr)
  {
    return pointerCast<const float, const Element>(ptr);
  }

 public:
  template <class T>
  void
  add(const T & element)
  {
    size_t pos          = data.size();
    size_t element_size = 1 + tagToCount(element.tag);
    data.resize(pos + element_size);
    Element * dst = floatPtrToElement(&data[pos]);
    memcpy(dst, &element, element_size * sizeof(float));
  }
};

// Sanity checks.
template <typename T>
static constexpr bool
checkElementSize()
{
  return sizeof(T) == 4u * (1u + Path::tagToCount(T::kTag));
};

static_assert(checkElementSize<Path::MoveTo>(), "invalid element count");
static_assert(checkElementSize<Path::LineTo>(), "invalid element count");
static_assert(checkElementSize<Path::QuadTo>(), "invalid element count");
static_assert(checkElementSize<Path::CubicTo>(), "invalid element count");
static_assert(checkElementSize<Path::RatQuadTo>(), "invalid element count");
static_assert(checkElementSize<Path::RatCubicTo>(), "invalid element count");

// Handy macros used to write path elements in a float array.
// This is especially useful for writing tests. For example, one can write
// something like:
//
//   static const float kExpectedPath[] = {
//      MOCK_SPINEL_PATH_MOVE_TO_LITERAL(0, 0),
//      MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 0),
//      MOCK_SPINEL_PATH_LINE_TO_LITERAL(16, 16),
//      MOCK_SPINEL_PATH_LINE_TO_LITERAL(0, 16),
//   };
//
//   ASSERT_THAT(my_path.data, ::testing::ElementAreArray(kExpectedPath));
//
#define MOCK_SPINEL_PATH_MOVE_TO_LITERAL(x, y) ::mock_spinel::Path::MoveTo::kTag, x, y

#define MOCK_SPINEL_PATH_LINE_TO_LITERAL(x, y) ::mock_spinel::Path::LineTo::kTag, x, y

#define MOCK_SPINEL_PATH_QUAD_TO_LITERAL(cx, cy, x, y)                                             \
  ::mock_spinel::Path::QuadTo::kTag, cx, cy, x, y

#define MOCK_SPINEL_PATH_CUBIC_TO_LITERAL(c1x, c1y, c2x, c2y, x, y)                                \
  ::mock_spinel::Path::CubicTo::kTag, c1x, c1y, c2x, c2y, x, y

#define MOCK_SPINEL_PATH_RAT_QUAD_TO_LITERAL(cx, cy, x, y, w)                                      \
  ::mock_spinel::Path::RatQuadTo::kTag, cx, cy, x, y, w

#define MOCK_SPINEL_PATH_RAT_CUBIC_TO_LITERAL(c1x, c1y, c2x, c2y, x, y, w1, w2)                    \
  ::mock_spinel::Path::RatCubicTo::kTag, c1x, c1y, c2x, c2y, x, y, w1, w2,

//
// Rasters
//

// A Spinel raster is modeled as an array of (path_id, transform, clip) tuples.
struct RasterPath
{
  uint32_t        path_id;
  spn_transform_t transform;
  spn_clip_t      clip;
};

using Raster = std::vector<RasterPath>;

class PathBuilder;
class RasterBuilder;
class Composition;
class Styling;

///
///  Context
///

class Context : public spinel_api::Context {
 public:
  Context() = default;

  spn_result_t
  reset() override;
  spn_result_t
  status() const override;

  spn_result_t
  createPathBuilder(spn_path_builder_t * path_builder) override;
  spn_result_t
  createRasterBuilder(spn_raster_builder_t * raster_builder) override;
  spn_result_t
  createComposition(spn_composition_t * composition) override;
  spn_result_t
  cloneComposition(spn_composition_t composition, spn_composition_t * clone) override;
  spn_result_t
  createStyling(uint32_t layers_count, uint32_t cmds_count, spn_styling_t * styling) override;

  spn_result_t
  retainPaths(const spn_path_t * ids, uint32_t count) override;
  spn_result_t
  releasePaths(const spn_path_t * ids, uint32_t count) override;
  spn_result_t
  retainRasters(const spn_raster_t * ids, uint32_t count) override;
  spn_result_t
  releaseRasters(const spn_raster_t * ids, uint32_t count) override;

  spn_result_t
  render(const spn_render_submit_t *) override;

  static Context *
  fromSpinel(spn_context_t context)
  {
    return reinterpret_cast<Context *>(context);
  }

  // Called from the path and raster builders to add a new path or raster instance
  // and return its Spinel handle.
  virtual spn_path_t
  installPath(Path && path);
  virtual spn_raster_t
  installRaster(Raster && raster);

  // Accessors useful during testing.
  const std::vector<Path>
  paths() const
  {
    return paths_;
  }
  const std::vector<Raster>
  rasters() const
  {
    return rasters_;
  }

  const Path *
  pathFor(spn_path_t handle);
  const Raster *
  rasterFor(spn_raster_t handle);

 protected:
  std::vector<Path>   paths_;
  std::vector<Raster> rasters_;
};

///
///  Path builder
///

class PathBuilder : public spinel_api::PathBuilder {
 public:
  explicit PathBuilder(Context * context) : context_(context)
  {
  }

  spn_result_t
  begin() override;
  spn_result_t
  moveTo(float x, float y) override;
  spn_result_t
  lineTo(float x, float y) override;
  spn_result_t
  quadTo(float cx, float cy, float x, float y) override;
  spn_result_t
  cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override;
  spn_result_t
  ratQuadTo(float cx, float cy, float x, float y, float w) override;
  spn_result_t
  ratCubicTo(
    float c1x, float c1y, float c2x, float c2y, float x, float y, float w1, float w2) override;
  spn_result_t
  end(spn_path_t * path) override;
  spn_result_t
  reset() override;
  spn_result_t
  flush() override;

  const Path &
  path() const
  {
    return path_;
  }

  static PathBuilder *
  fromSpinel(spn_path_builder_t path_builder)
  {
    return reinterpret_cast<PathBuilder *>(path_builder);
  }

 protected:
  Context * context_;
  Path      path_;
};

///
///  Raster builder
///

class RasterBuilder : public spinel_api::RasterBuilder {
 public:
  explicit RasterBuilder(Context * context) : context_(context)
  {
  }

  spn_result_t
  begin() override;
  spn_result_t
  end(spn_raster_t * raster) override;
  spn_result_t
  add(spn_path_t const *        paths,
      spn_transform_weakref_t * transform_weakrefs,
      spn_transform_t const *   transforms,
      spn_clip_weakref_t *      clip_weakrefs,
      spn_clip_t const *        clips,
      uint32_t                  count) override;
  spn_result_t
  flush() override;

  static RasterBuilder *
  fromSpinel(spn_raster_builder_t raster_builder)
  {
    return reinterpret_cast<RasterBuilder *>(raster_builder);
  }

 protected:
  Context * context_;
  Raster    raster_;
};

///
///  Composition
///

struct RasterPrint
{
  uint32_t     raster_id;
  spn_layer_id layer_id;
  spn_txty_t   translation;
};

class Composition : public spinel_api::Composition {
 public:
  Composition(Context * context) : context_(context)
  {
  }

  Composition *
  clone() override;

  spn_result_t
  place(spn_raster_t const * rasters,
        spn_layer_id const * layer_ids,
        spn_txty_t const *   txtys,
        uint32_t             count) override;

  spn_result_t
  seal() override;
  spn_result_t
  unseal() override;
  spn_result_t
  reset() override;
  spn_result_t
  getBounds(uint32_t bounds[4]) const override;
  spn_result_t
  setClip(const uint32_t clip[4]) override;

  const std::vector<RasterPrint> &
  prints() const
  {
    return prints_;
  }

  // Return a map from layer id -> vector of RasterPrint pointers.
  // the map is only valid until the composition is modified.
  using LayerMap = std::map<uint32_t, std::vector<const RasterPrint *>>;
  LayerMap
  computeLayerMap() const;

  static Composition *
  fromSpinel(spn_composition_t composition)
  {
    return reinterpret_cast<Composition *>(composition);
  }

 protected:
  Context *                context_;
  std::vector<RasterPrint> prints_;
};

///
///  Styling
///

using StylingCommands = std::vector<spn_styling_cmd_t>;

struct StylingGroup
{
  spn_layer_id                        layer_lo;
  spn_layer_id                        layer_hi;
  StylingCommands                     begin_commands;
  StylingCommands                     end_commands;
  std::map<uint32_t, StylingCommands> layer_commands;
};

class Styling : public spinel_api::Styling {
 public:
  explicit Styling(Context * context, uint32_t layers_count, uint32_t cmds_count)
  {
    UNUSED(context);
    UNUSED(layers_count);
    UNUSED(cmds_count);
  }

  spn_result_t
  seal() override;
  spn_result_t
  unseal() override;
  spn_result_t
  reset() override;
  spn_result_t
  groupAllocId(spn_group_id * group_id) override;

  spn_result_t
  groupAllocEnterCommands(spn_group_id         group_id,
                          uint32_t             count,
                          spn_styling_cmd_t ** cmds) override;

  spn_result_t
  groupAllocLeaveCommands(spn_group_id         group_id,
                          uint32_t             count,
                          spn_styling_cmd_t ** cmds) override;

  spn_result_t
  groupAllocParents(spn_group_id group_id, uint32_t count, spn_group_id ** parents) override;

  spn_result_t
  groupAllocLayerCommands(spn_group_id         group_id,
                          spn_layer_id         layer_id,
                          uint32_t             count,
                          spn_styling_cmd_t ** cmds) override;

  spn_result_t
  groupSetRangeLo(spn_group_id group_id, spn_layer_id layer_lo) override;
  spn_result_t
  groupSetRangeHi(spn_group_id group_id, spn_layer_id layer_hi) override;

  // For testing.
  const std::vector<StylingGroup> &
  groups() const
  {
    return groups_;
  }

  static Styling *
  fromSpinel(spn_styling_t styling)
  {
    return reinterpret_cast<Styling *>(styling);
  }

 protected:
  std::vector<StylingGroup> groups_;
};

///
///  Interface
///

class Spinel : public spinel_api::Interface {
 public:
  // Create a context backed by a mock_spinel::Spinel implementation.
  static spn_result_t
  createContext(spn_context_t * context);

  // Encode 4 floating-point rgba values into two 32-bit command words.
  static void
  rgbaToCmds(const float rgba[4], spn_styling_cmd_t cmds[2]);

  // Decode two 32-bit command words into 4 floating-point rgba values.
  static void
  cmdsToRgba(const spn_styling_cmd_t cmds[2], float rgba[4]);

  void
  encodeCommandFillRgba(spn_styling_cmd_t *, const float rgba[4]) override;

  void
  encodeCommandBackgroundOver(spn_styling_cmd_t *, const float rgba[4]) override;
};

}  // namespace mock_spinel

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_MOCK_SPINEL_MOCK_SPINEL_H_
