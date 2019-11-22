// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg2spinel.h"

#include <gtest/gtest.h>

//
// clang-format off
//

#include "core_c.h"  // needs to be included before core.h

//
// clang-format on
//

#include "composition.h"
#include "context.h"
#include "core.h"
#include "path_builder.h"
#include "raster_builder.h"
#include "styling.h"
#include "svg/svg.h"

//
//
//

spn_result_t
spn_pbi_begin(struct spn_path_builder_impl * const impl);

spn_result_t
spn_pbi_end(struct spn_path_builder_impl * const impl, spn_path_t * const path);

#define SPN_PBI_PFN_NAME(_p) spn_pbi_##_p
#define SPN_PBI_VEC_NAME(_p) _p##_vec

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  spn_result_t SPN_PBI_PFN_NAME(_p)(struct spn_path_builder_impl * const impl);

SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

struct spn_path_builder_impl
{
  spn_path_builder_impl()
  {
    pb.impl  = this;
    pb.begin = spn_pbi_begin;
    pb.end   = spn_pbi_end;

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb._p = SPN_PBI_PFN_NAME(_p);

    SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

    SPN_ASSERT_STATE_INIT(SPN_PATH_BUILDER_STATE_READY, &pb);
  }

  spn_path_builder pb{};

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) std::vector<float> SPN_PBI_VEC_NAME(_p);

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()
};

spn_result_t
spn_pbi_begin(struct spn_path_builder_impl * const impl)
{
  struct spn_path_builder * const pb = &impl->pb;

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n) pb->cn.rem._p = 0;

  SPN_PATH_BUILDER_PRIM_TYPE_EXPAND();

  return SPN_SUCCESS;
}

spn_result_t
spn_pbi_end(struct spn_path_builder_impl * const impl, spn_path_t * const path)
{
  path->handle = 0;
  return SPN_SUCCESS;
}

#undef SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X
#define SPN_PATH_BUILDER_PRIM_TYPE_EXPAND_X(_p, _i, _n)                                            \
  spn_result_t SPN_PBI_PFN_NAME(_p)(struct spn_path_builder_impl * const impl)                     \
  {                                                                                                \
    float ** coords     = impl->pb.cn.coords._p;                                                   \
    uint32_t coords_len = _n;                                                                      \
    uint32_t offset     = impl->SPN_PBI_VEC_NAME(_p).size();                                       \
    impl->SPN_PBI_VEC_NAME(_p).resize(offset + coords_len, NAN);                                   \
    float * data = impl->SPN_PBI_VEC_NAME(_p).data() + offset;                                     \
    do                                                                                             \
      {                                                                                            \
        *coords++ = data++;                                                                        \
      }                                                                                            \
    while (--coords_len > 0);                                                                      \
    impl->pb.cn.rem.aN[_i] = 1;                                                                    \
    return SPN_SUCCESS;                                                                            \
  }

SPN_PATH_BUILDER_PRIM_TYPE_EXPAND()

//
//
//

spn_result_t
spn_rbi_begin(struct spn_raster_builder_impl * const impl);

spn_result_t
spn_rbi_end(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster);

spn_result_t
spn_rbi_add(struct spn_raster_builder_impl * const impl,
            spn_path_t const *                     paths,
            spn_transform_weakref_t *              transform_weakrefs,
            spn_transform_t const *                transforms,
            spn_clip_weakref_t *                   clip_weakrefs,
            spn_clip_t const *                     clips,
            uint32_t                               count);

struct spn_raster_builder_impl
{
  spn_raster_builder_impl()
  {
    rb.impl  = this;
    rb.begin = spn_rbi_begin;
    rb.end   = spn_rbi_end;
    rb.add   = spn_rbi_add;

    SPN_ASSERT_STATE_INIT(SPN_RASTER_BUILDER_STATE_READY, &rb);
  }

  spn_raster_builder           rb{};
  std::vector<spn_transform_t> transform_vec;
};

spn_result_t
spn_rbi_begin(struct spn_raster_builder_impl * const impl)
{
  return SPN_SUCCESS;
}

spn_result_t
spn_rbi_end(struct spn_raster_builder_impl * const impl, spn_raster_t * const raster)
{
  return SPN_SUCCESS;
}

spn_result_t
spn_rbi_add(struct spn_raster_builder_impl * const impl,
            spn_path_t const *                     paths,
            spn_transform_weakref_t *              transform_weakrefs,
            spn_transform_t const *                transforms,
            spn_clip_weakref_t *                   clip_weakrefs,
            spn_clip_t const *                     clips,
            uint32_t                               count)
{
  do
    {
      impl->transform_vec.push_back(*transforms++);
    }
  while (--count > 0);
  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_ci_place(struct spn_composition_impl * const impl,
             spn_raster_t const *                rasters,
             spn_layer_id const *                layer_ids,
             spn_txty_t const *                  txtys,
             uint32_t                            count);

struct spn_composition_impl
{
  spn_composition_impl()
  {
    c.impl  = this;
    c.place = spn_ci_place;
  }

  spn_composition           c{};
  std::vector<spn_layer_id> layer_id_vec;
};

spn_result_t
spn_ci_place(struct spn_composition_impl * const impl,
             spn_raster_t const *                rasters,
             spn_layer_id const *                layer_ids,
             spn_txty_t const *                  txtys,
             uint32_t                            count)
{
  do
    {
      impl->layer_id_vec.push_back(*layer_ids++);
    }
  while (--count > 0);
  return SPN_SUCCESS;
}

//
//
//

spn_result_t
spn_si_unseal(struct spn_styling_impl * const impl);

struct spn_styling_impl
{
  spn_styling_impl(uint32_t const layers_count, uint32_t const cmds_count)
  {
    s.impl   = this;
    s.unseal = spn_si_unseal;

    s.layers.count = layers_count;

    uint32_t const layers_dwords = layers_count * SPN_STYLING_LAYER_COUNT_DWORDS;
    uint32_t const dwords_count  = layers_dwords + cmds_count;

    s.dwords.count = dwords_count;
    s.dwords.next  = layers_dwords;

    dwords_vec.resize(dwords_count, 0xdedbeef);
    s.extent = dwords_vec.data();
  }

  spn_styling           s{};
  std::vector<uint32_t> dwords_vec;
};

spn_result_t
spn_si_unseal(struct spn_styling_impl * const impl)
{
  return SPN_SUCCESS;
}

namespace {

//
//
//

spn_result_t
spn_ctx_release_paths(struct spn_device * const     device,
                      struct spn_path const * const paths,
                      uint32_t const                count)
{
  return SPN_SUCCESS;
}

spn_result_t
spn_ctx_release_rasters(struct spn_device * const       device,
                        struct spn_raster const * const rasters,
                        uint32_t const                  count)
{
  return SPN_SUCCESS;
}

struct spn_test_context
{
  spn_test_context(uint32_t const layers_count, uint32_t const cmds_count)
      : styling(layers_count, cmds_count)
  {
    ctx.path_release   = spn_ctx_release_paths;
    ctx.raster_release = spn_ctx_release_rasters;
  }

  spn_context             ctx;
  spn_path_builder_impl   path_builder;
  spn_raster_builder_impl raster_builder;
  spn_composition_impl    composition;
  spn_styling_impl        styling;
};

//
//
//

TEST(svg2spinel, polyline)
{
  char const doc[] = { "<svg xmlns=\"http://www.w3.org/2000/svg\">\n"
                       "  <g style = \"fill: #FF0000\">\n"
                       "    <polyline points = \"0,0 16,0 16,16 0,16 0,0\"/>\n"
                       "  </g>\n"
                       "</svg>\n" };

  svg * svg = svg_parse(doc, false);
  ASSERT_TRUE(svg);

  uint32_t const layer_count = svg_layer_count(svg);

  spn_test_context context(layer_count, 11 + layer_count * 6);

  spn_path_t * paths = spn_svg_paths_decode(svg, &context.path_builder.pb);
  ASSERT_TRUE(paths);

  // Verify line coordinates for polyline path.
  EXPECT_EQ(context.path_builder.line_vec.size(), 16u);
  EXPECT_EQ(context.path_builder.line_vec[0], 0);
  EXPECT_EQ(context.path_builder.line_vec[1], 0);
  EXPECT_EQ(context.path_builder.line_vec[2], 16);
  EXPECT_EQ(context.path_builder.line_vec[3], 0);
  EXPECT_EQ(context.path_builder.line_vec[4], 16);
  EXPECT_EQ(context.path_builder.line_vec[5], 0);
  EXPECT_EQ(context.path_builder.line_vec[6], 16);
  EXPECT_EQ(context.path_builder.line_vec[7], 16);
  EXPECT_EQ(context.path_builder.line_vec[8], 16);
  EXPECT_EQ(context.path_builder.line_vec[9], 16);
  EXPECT_EQ(context.path_builder.line_vec[10], 0);
  EXPECT_EQ(context.path_builder.line_vec[11], 16);
  EXPECT_EQ(context.path_builder.line_vec[13], 16);
  EXPECT_EQ(context.path_builder.line_vec[14], 0);
  EXPECT_EQ(context.path_builder.line_vec[15], 0);

  transform_stack * ts = transform_stack_create(32);
  transform_stack_push_identity(ts);

  spn_raster_t * rasters = spn_svg_rasters_decode(svg, &context.raster_builder.rb, paths, ts);
  ASSERT_TRUE(rasters);

  // Verify transform for polyline raster.
  EXPECT_EQ(context.raster_builder.transform_vec.size(), 1u);
  EXPECT_EQ(context.raster_builder.transform_vec[0].sx, 1.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].shx, 0.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].tx, 0.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].shy, 0.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].sy, 1.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].ty, 0.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].w0, 0.0);
  EXPECT_EQ(context.raster_builder.transform_vec[0].w1, 0.0);

  spn_svg_layers_decode(svg, rasters, &context.composition.c, &context.styling.s, true);

  // Verify layer count for composition.
  EXPECT_EQ(context.composition.layer_id_vec.size(), 1u);
  EXPECT_EQ(context.composition.layer_id_vec[0], 0u);

  // Verify styling cmds for group.
  EXPECT_EQ(context.styling.dwords_vec[13 + 0],
            static_cast<uint32_t>(SPN_STYLING_OPCODE_COVER_NONZERO));
  EXPECT_EQ(context.styling.dwords_vec[13 + 1],
            static_cast<uint32_t>(SPN_STYLING_OPCODE_COLOR_FILL_SOLID));
  EXPECT_EQ(context.styling.dwords_vec[13 + 2], 0x00003c00u);
  EXPECT_EQ(context.styling.dwords_vec[13 + 3], 0x3c000000u);
  EXPECT_EQ(context.styling.dwords_vec[13 + 4],
            static_cast<uint32_t>(SPN_STYLING_OPCODE_BLEND_OVER));

  transform_stack_release(ts);

  spn_svg_rasters_release(svg, &context.ctx, rasters);
  spn_svg_paths_release(svg, &context.ctx, paths);

  svg_dispose(svg);
}

}  // namespace
