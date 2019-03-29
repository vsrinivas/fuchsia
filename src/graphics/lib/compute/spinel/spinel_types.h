// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SPN_ONCE_SPINEL_TYPES
#define SPN_ONCE_SPINEL_TYPES

//
//
//

#include <stdint.h>
#include <stdbool.h>

//
//
//

typedef struct spn_context        * spn_context_t;
typedef struct spn_path_builder   * spn_path_builder_t;
typedef struct spn_raster_builder * spn_raster_builder_t;

typedef struct spn_composition    * spn_composition_t;
typedef struct spn_styling        * spn_styling_t;

typedef struct spn_surface        * spn_surface_t;

typedef        uint32_t             spn_path_t;
typedef        uint32_t             spn_raster_t;

typedef        uint32_t             spn_layer_id;
typedef        uint32_t             spn_group_id;

typedef        uint32_t             spn_styling_cmd_t;

typedef        uint64_t             spn_weakref_t;
typedef        spn_weakref_t        spn_transform_weakref_t;
typedef        spn_weakref_t        spn_clip_weakref_t;

typedef        void               * spn_framebuffer_t;

//
//
//

#define SPN_PATH_INVALID               UINT32_MAX
#define SPN_RASTER_INVALID             UINT32_MAX

#define SPN_WEAKREF_INVALID            0UL
#define SPN_TRANSFORM_WEAKREF_INVALID  SPN_WEAKREF_INVALID
#define SPN_CLIP_WEAKREF_INVALID       SPN_WEAKREF_INVALID

//
// TRANSFORM LAYOUT: { sx shx tx shy sy ty w0 w1 }
//

//
// RASTER CLIP LAYOUT: { x0, y0, x1, y1 }
//

//
// RENDER
//
// Render a composition and styling to a surface defined in the
// extension chain.
//

typedef struct spn_render_submit
{
  void              * ext;
  spn_composition_t   composition;
  spn_styling_t       styling;
  uint32_t            clip[4];
} spn_render_submit_t;

//
// RENDER EXTENSIONS
//

typedef enum spn_render_submit_ext_type_e
{
  SPN_RENDER_SUBMIT_EXT_TYPE_WAIT,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_BUFFER,
  SPN_RENDER_SUBMIT_EXT_TYPE_VK_IMAGE,

} spn_render_submit_ext_type_e;

//
// If wait is true then block until the render completes.
//

struct spn_render_submit_ext_wait
{
  void                         * ext;
  spn_render_submit_ext_type_e   type;
  bool                           wait;
};

//
//
//

#if 0 // FIXME -- make this private

struct spn_render_submit_ext_base_in
{
  struct spn_render_submit_ext_base_in const * next;
  spn_render_submit_ext_type_e                 type;
};

#endif

//
//
//

#endif
