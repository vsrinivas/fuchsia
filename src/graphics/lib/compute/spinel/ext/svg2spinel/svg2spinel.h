// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_SVG2SPINEL_SVG2SPINEL_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_SVG2SPINEL_SVG2SPINEL_H_

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

spn_path_t *
spn_svg_paths_decode(struct svg const * const svg, spn_path_builder_t pb);

//
// Defines all rasters in the SVG doc
//

spn_raster_t *
spn_svg_rasters_decode(struct svg const * const       svg,
                       spn_raster_builder_t           rb,
                       spn_path_t const * const       paths,
                       struct transform_stack * const ts);

//
// Defines the styling and composition raster placement for the SVG doc
//

void
spn_svg_layers_decode(struct svg const * const   svg,
                      spn_raster_t const * const rasters,
                      spn_composition_t          composition,
                      spn_styling_t              styling,
                      bool const                 is_srgb);

//
// Releases all paths in the SVG doc
//

void
spn_svg_paths_release(struct svg const * const svg,
                      spn_context_t            context,
                      spn_path_t * const       paths);

//
// Releases all rasters in the SVG doc
//

void
spn_svg_rasters_release(struct svg const * const svg,
                        spn_context_t            context,
                        spn_raster_t * const     rasters);

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_EXT_SVG2SPINEL_SVG2SPINEL_H_

//
//
//
