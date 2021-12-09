// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_PIPELINES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_PIPELINES_H_

//
// Pipelines (always in alphabetical order)
//
//   1. Pipeline name
//   2. Push constant struct name
//
// clang-format off
#define SPN_P_EXPAND()                                            \
  SPN_P_EXPAND_X(block_pool_init,        block_pool_init        ) \
  SPN_P_EXPAND_X(fill_dispatch,          fill_dispatch          ) \
  SPN_P_EXPAND_X(fill_expand,            fill_expand            ) \
  SPN_P_EXPAND_X(fill_scan,              fill_scan              ) \
  SPN_P_EXPAND_X(paths_alloc,            paths_alloc            ) \
  SPN_P_EXPAND_X(paths_copy,             paths_copy             ) \
  SPN_P_EXPAND_X(paths_reclaim,          reclaim                ) \
  SPN_P_EXPAND_X(place_ttpk,             place                  ) \
  SPN_P_EXPAND_X(place_ttsk,             place                  ) \
  SPN_P_EXPAND_X(rasterize_cubic,        rasterize              ) \
  SPN_P_EXPAND_X(rasterize_line,         rasterize              ) \
  SPN_P_EXPAND_X(rasterize_proj_cubic,   rasterize              ) \
  SPN_P_EXPAND_X(rasterize_proj_line,    rasterize              ) \
  SPN_P_EXPAND_X(rasterize_proj_quad,    rasterize              ) \
  SPN_P_EXPAND_X(rasterize_quad,         rasterize              ) \
  SPN_P_EXPAND_X(rasterize_rat_cubic,    rasterize              ) \
  SPN_P_EXPAND_X(rasterize_rat_quad,     rasterize              ) \
  SPN_P_EXPAND_X(rasters_alloc,          rasters_alloc          ) \
  SPN_P_EXPAND_X(rasters_prefix,         rasters_prefix         ) \
  SPN_P_EXPAND_X(rasters_reclaim,        reclaim                ) \
  SPN_P_EXPAND_X(render,                 render                 ) \
  SPN_P_EXPAND_X(render_dispatch,        render_dispatch        ) \
  SPN_P_EXPAND_X(ttcks_segment,          ttcks_segment          ) \
  SPN_P_EXPAND_X(ttcks_segment_dispatch, ttcks_segment_dispatch ) \
  SPN_P_EXPAND_X(ttrks_segment,          ttrks_segment          ) \
  SPN_P_EXPAND_X(ttrks_segment_dispatch, ttrks_segment_dispatch )
// clang-format on

//
//
//

#define SPN_P_COUNT 26  // Note: this count is validated at compile time.

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_SHADERS_PIPELINES_H_
