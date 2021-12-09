// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_SVG2SPINEL_SVG2SPINEL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_SVG2SPINEL_SVG2SPINEL_H_

//
// This adapter reads commands from the SVG doc dictionaries and applies
// them to the Spinel API.
//

#include "spinel/ext/transform_stack/transform_stack.h"
#include "spinel/spinel.h"
#include "svg/svg.h"

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
// Defines all paths in the SVG doc
//

spinel_path_t *
spinel_svg_paths_decode(struct svg const * const svg, spinel_path_builder_t pb);

//
// Defines all rasters in the SVG doc
//

spinel_raster_t *
spinel_svg_rasters_decode(struct svg const * const              svg,
                          spinel_raster_builder_t               rb,
                          spinel_path_t const * const           paths,
                          struct spinel_transform_stack * const ts);

//
// Defines the styling and composition raster placement for the SVG doc
//

void
spinel_svg_layers_decode(struct svg const * const      svg,
                         spinel_raster_t const * const rasters,
                         spinel_composition_t          composition,
                         spinel_styling_t              styling,
                         bool const                    is_srgb);

//
// Releases all paths in the SVG doc
//

void
spinel_svg_paths_release(struct svg const * const svg,
                         spinel_context_t         context,
                         spinel_path_t * const    paths);

//
// Releases all rasters in the SVG doc
//

void
spinel_svg_rasters_release(struct svg const * const svg,
                           spinel_context_t         context,
                           spinel_raster_t * const  rasters);

//
// ADVANCED OPERATIONS
//

//
// Defines the styling and composition raster placement for multiple SVG
// docs.
//

void
spinel_svg_layers_decode_n(uint32_t const                svg_count,
                           struct svg const * const      svgs[],
                           spinel_raster_t const * const rasters[],
                           spinel_composition_t          composition,
                           spinel_styling_t              styling,
                           bool const                    is_srgb);

//
// Define the styling and composition raster placement for an SVG at a
// specific location in the styling hierarchy.
//

void
spinel_svg_layers_decode_at(spinel_layer_id const         layer_base,
                            spinel_group_id const         group_id,
                            struct svg const * const      svg,
                            spinel_raster_t const * const rasters,
                            spinel_composition_t          composition,
                            spinel_styling_t              styling,
                            bool const                    is_srgb);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_INCLUDE_SPINEL_EXT_SVG2SPINEL_SVG2SPINEL_H_

//
//
//
