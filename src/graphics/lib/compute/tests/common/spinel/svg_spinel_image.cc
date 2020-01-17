// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "svg_spinel_image.h"

#include "spinel/ext/svg2spinel/svg2spinel.h"
#include "spinel/ext/transform_stack/transform_stack.h"
#include "spinel/spinel.h"
#include "spinel/spinel_assert.h"
#include "tests/common/utils.h"  // For ASSERT_MSG()

void
SvgSpinelImage::init(const svg * svg, spn_context_t context, const SpinelImage::Config & config)
{
  svg_ = svg;
  SpinelImage::init(context, config);
}

void
SvgSpinelImage::init(const svg * svg, spn_context_t context)
{
  svg_ = svg;
  SpinelImage::init(context);
}

void
SvgSpinelImage::reset()
{
  resetLayers();
  resetRasters();
  resetPaths();
  SpinelImage::reset();
}

void
SvgSpinelImage::setupPaths()
{
  ASSERT_MSG(!paths_, "Cannot call setupPaths() twice without resetPaths()");
  paths_ = spn_svg_paths_decode(svg_, path_builder);
}

void
SvgSpinelImage::resetPaths()
{
  if (paths_)
    {
      spn_svg_paths_release(svg_, context, paths_);
      paths_ = nullptr;
    }
}

void
SvgSpinelImage::setupRasters(const spn_transform_t * transform)
{
  ASSERT_MSG(!rasters_, "Cannot call setupRasters() twice without resetRasters()");

  if (transform)
    {
      transform_stack_push_matrix(transform_stack,
                                  transform->sx,
                                  transform->shx,
                                  transform->tx,
                                  transform->shy,
                                  transform->sy,
                                  transform->ty,
                                  transform->w0,
                                  transform->w1,
                                  1.0);
      transform_stack_concat(transform_stack);
    }

  rasters_ = spn_svg_rasters_decode(svg_, raster_builder, paths_, transform_stack);

  if (transform)
    transform_stack_drop(transform_stack);
}

void
SvgSpinelImage::resetRasters()
{
  if (rasters_)
    {
      spn_svg_rasters_release(svg_, context, rasters_);
      rasters_ = nullptr;
    }
}

void
SvgSpinelImage::setupLayers()
{
  spn_svg_layers_decode(svg_, rasters_, composition, styling, true);

  spn(styling_seal(styling));
  spn(composition_seal(composition));
}

void
SvgSpinelImage::resetLayers()
{
  if (styling)
    {
      spn(styling_unseal(styling));
      spn(styling_reset(styling));
    }

  if (composition)
    {
      spn(composition_unseal(composition));
      spn(composition_reset(composition));
    }
}
