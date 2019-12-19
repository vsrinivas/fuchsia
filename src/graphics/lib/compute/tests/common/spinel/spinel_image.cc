// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spinel_image.h"

#include <new>

#include "spinel/ext/transform_stack/transform_stack.h"
#include "spinel/spinel.h"
#include "spinel/spinel_assert.h"

// static
void
SpinelImage::init(spn_context_t context, const SpinelImage::Config & config)
{
  this->context = context;

  // BUG: spn_context_retain() doesn't do anything, while spn_context_release()
  //      destroys the context immediately.
  //spn(context_retain(context));

  spn(path_builder_create(context, &this->path_builder));
  spn(raster_builder_create(context, &this->raster_builder));
  spn(composition_create(context, &this->composition));

  this->transform_stack = transform_stack_create(16);
  transform_stack_push_scale(this->transform_stack, 32., 32.);

  spn(composition_set_clip(this->composition, config.clip));
  spn(styling_create(this->context,
                     &this->styling,
                     config.max_layer_count,
                     config.max_commands_count));
}

// static
void
SpinelImage::init(spn_context_t context)
{
  init(context, Config());
}

void
SpinelImage::reset()
{
  if (styling)
    {
      spn(styling_release(styling));
      styling = nullptr;
    }
  if (composition)
    {
      spn(composition_release(composition));
      composition = nullptr;
    }
  if (path_builder)
    {
      spn(path_builder_release(path_builder));
      path_builder = nullptr;
    }
  if (raster_builder)
    {
      spn(raster_builder_release(raster_builder));
      raster_builder = nullptr;
    }
  if (transform_stack)
    {
      transform_stack_release(transform_stack);
      transform_stack = nullptr;
    }

  // BUG: See above.
  //spn_context_release(context);
  context = nullptr;
}

void
SpinelImage::render(void * submit_ext, uint32_t width, uint32_t height)
{
  const spn_render_submit_t submit = {
    .ext         = submit_ext,
    .styling     = styling,
    .composition = composition,
    .clip        = { 0, 0, width, height },
  };
  spn(render(context, &submit));
}
