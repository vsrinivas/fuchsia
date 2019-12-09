#include <gtest/gtest.h>

#include "mold/mold.h"
#include "spinel/spinel.h"
#include "spinel/spinel_opcodes.h" 

#include <float.h>

TEST(MoldTest, ContextCreation)
{
  spn_context_t context;
  mold_context_create(&context);
  spn_context_release(context);
}

#define ASSERT_SPN(...)  ASSERT_EQ(SPN_SUCCESS, __VA_ARGS__)
#define EXPECT_SPN(...)  EXPECT_EQ(SPN_SUCCESS, __VA_ARGS__)

#if 0
// TODO(digit): Enable when <gmock/gmock.h> is available.
static ::testing::AssertionResult PixelMatchesRGB(const char*     pixel_expr,
                                                  const char*     rgb_expr,
                                                  const uint8_t*  pixel,
                                                  uint32_t        rgb)
{
  uint32_t pixel_rgb = ((uint32_t)pixel[0] << 24) |
                       ((uint32_t)pixel[1] << 16) |
                       ((uint32_t)pixel[2] << 8);
  if (pixel_rgb == rgb)
    return ::testing::AssertionSuccess();

  return ::testing::AssertionFailure()
      << "Pixel value (" << pixel[0] << "," << pixel[1] << "," << pixel[2]
      << ") does not match expected ("
      << (rgb >> 16) & 255 << ","
      << (rgb >> 8) & 255 << ","
      << (rgb & 255) << ")";
}
#endif  // 0

TEST(MoldTest, Render2x2BlackSquare)
{
  spn_context_t context;
  mold_context_create(&context);

  {
    spn_path_builder_t path_builder;
    spn_raster_builder_t raster_builder;

    ASSERT_SPN(spn_path_builder_create(context, &path_builder));
    ASSERT_SPN(spn_raster_builder_create(context, &raster_builder));

    // Create one path that is a 2x2 square.
    spn_path_t path;
    ASSERT_SPN(spn_path_builder_begin(path_builder));
    ASSERT_SPN(spn_path_builder_move_to(path_builder, 0., 0.));
    ASSERT_SPN(spn_path_builder_line_to(path_builder, 0., 2.));
    ASSERT_SPN(spn_path_builder_line_to(path_builder, 2., 2.));
    ASSERT_SPN(spn_path_builder_line_to(path_builder, 2., 0.));
    ASSERT_SPN(spn_path_builder_line_to(path_builder, 0., 0.));
    ASSERT_SPN(spn_path_builder_end(path_builder, &path));

    // Create one raster with identity transform and no clip.
    spn_raster_t raster;

    // Transform from path coordinates to Spinel sub-pixel space.
    spn_transform_t transform = {
      .sx = 32.,
      .sy = 32.,
    };

    spn_clip_t clip = { 0., 0., FLT_MAX, FLT_MAX };

    ASSERT_SPN(spn_raster_builder_begin(raster_builder));
    ASSERT_SPN(spn_raster_builder_add(raster_builder,
                                      &path,
                                      NULL,
                                      &transform,
                                      NULL,
                                      &clip,
                                      1));
    ASSERT_SPN(spn_raster_builder_end(raster_builder, &raster));

    // Create one composition with a single layer/raster in it.
    spn_composition_t composition;
    ASSERT_SPN(spn_composition_create(context, &composition));

    spn_layer_id layer = 42u;
    spn_txty_t   translation = { 0u, 0u };

    ASSERT_SPN(spn_composition_place(composition,
                                     &raster,
                                     &layer,
                                     &translation,
                                     1));
    // Ensure it is filled with black.
    spn_styling_t styling;
    ASSERT_SPN(spn_styling_create(context, &styling, 128u, 32u));
    spn_group_id group;
    spn_styling_cmd_t* begin_cmds;
    spn_styling_cmd_t* end_cmds;
    spn_styling_cmd_t* layer_cmds;

    ASSERT_SPN(spn_styling_group_alloc(styling, &group));
    ASSERT_SPN(spn_styling_group_range_lo(styling, group, layer));
    ASSERT_SPN(spn_styling_group_range_hi(styling, group, layer));

    ASSERT_SPN(spn_styling_group_parents(styling, group, 0, NULL));

    const float black_color[4] = { 0., 0., 0., 1. };
    const float white_color[4] = { 1., 1., 1., 1. };

    ASSERT_SPN(spn_styling_group_enter(styling, group, 1, &begin_cmds));
    begin_cmds[0]  = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;

    ASSERT_SPN(spn_styling_group_layer(styling, group, layer, 7, &layer_cmds));
    layer_cmds[0] = SPN_STYLING_OPCODE_COVER_WIP_ZERO;
    layer_cmds[1] = SPN_STYLING_OPCODE_COLOR_WIP_ZERO;
    layer_cmds[2] = SPN_STYLING_OPCODE_COVER_NONZERO;
    spn_styling_layer_fill_rgba_encoder(layer_cmds + 3, black_color);
    layer_cmds[6] = SPN_STYLING_OPCODE_BLEND_OVER;

    ASSERT_SPN(spn_styling_group_leave(styling, group, 4, &end_cmds));
    spn_styling_background_over_encoder(end_cmds, white_color);
    end_cmds[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;

    // Seal everything then render to small 4x4 buffer that is initially filled
    // with specific non-black colors.

    ASSERT_SPN(spn_composition_seal(composition));
    ASSERT_SPN(spn_styling_seal(styling));

    const uint32_t buffer_width = 4;
    const uint32_t buffer_height = 4;
    const uint32_t buffer_stride = buffer_width * 4;
    uint8_t buffer[buffer_height * buffer_stride];

    mold_raw_buffer_t target_buffer = {
      .buffer = buffer,
      .width  = buffer_width,
      .format = MOLD_RGBA8888,
    };
    spn_render_submit_t submit = {
      .ext = &target_buffer,
      .styling = styling,
      .composition = composition,
      .clip = { 0, 0, buffer_width, buffer_height },
    };
    ASSERT_SPN(spn_render(context, &submit));

    // Verify the buffer content.
    //
    // IMPORTANT: Ignore the alpha values in the target buffer, they will
    //            be invalid most of the time (in both Spinel and Mold)!!

    for (size_t yy = 0; yy < buffer_height; ++yy) {
      for (size_t xx = 0; xx < buffer_width; ++xx) {
        size_t offset = yy * buffer_stride + xx * 4;
        const uint8_t* pixel = buffer + offset;
        if (xx < 2 && yy < 2) {
          EXPECT_EQ(pixel[0], 0u) << "at " << xx << "," << yy;
          EXPECT_EQ(pixel[1], 0u) << "at " << xx << "," << yy;
          EXPECT_EQ(pixel[2], 0u) << "at " << xx << "," << yy;
        } else {
          EXPECT_EQ(pixel[0], 255u) << "at " << xx << "," << yy;
          EXPECT_EQ(pixel[1], 255u) << "at " << xx << "," << yy;
          EXPECT_EQ(pixel[2], 255u) << "at " << xx << "," << yy;
        }
      }
    }

    // Dispose of resources.
    ASSERT_SPN(spn_styling_unseal(styling));
    ASSERT_SPN(spn_styling_release(styling));

    ASSERT_SPN(spn_styling_unseal(styling));
    ASSERT_SPN(spn_composition_release(composition));
    ASSERT_SPN(spn_raster_builder_release(raster_builder));
    ASSERT_SPN(spn_path_builder_release(path_builder));
  }

  spn_context_release(context);
}
