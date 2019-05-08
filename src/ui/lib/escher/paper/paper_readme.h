// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_README_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_README_H_

#include "src/ui/lib/escher/forward_declarations.h"

// This file serves as a high-level overview of the types that comprise the
// "Paper" subsystem of Escher and their inter-relationships, and simultaneously
// provides forward-declarations of these types.
//
// The main goal of the "Paper" subsystem is to provide a convenient yet
// flexible API to meet Scenic's immediate rendering needs.  It is also a step
// toward distinguishing fundamental Escher concepts/functionality from higher
// layers which serve other goals (experimental, domain-specific, etc).

namespace escher {

// PaperRenderer knows how to render PaperDrawables to an output framebuffer.
// Clients configure the renderer's behavior by setting a config object.
class PaperRenderer;
struct PaperRendererConfig;

// PaperDrawable is a pure virtual interface with a single DrawInScene() method
// that is invoked by PaperRenderer::Draw().  Clients may use pre-existing
// implementations of PaperDrawable, or roll their own.
//
// Currently, PaperLegacyDrawable is the only standard implementation of
// PaperDrawable.  It allows PaperRenderer to draw "legacy" escher::Objects.
class PaperDrawable;
class PaperLegacyDrawable;

// PaperTransformStack is a helper class to be used along with PaperRenderer
// when rendering hierarchical scenes.  It maintains a stack where each item has
// two fields:
//   - a 4x4 model-to-world transform matrix
//   - a list of model-space clip planes
class PaperTransformStack;

// PaperScene describes high-level scene parameters, such the number of point
// lights and their parameters, and the scene's bounding-box.
class PaperScene;

// Placeholder for a real PaperMaterial type.
using PaperMaterial = Material;

// RefPtr forward declarations.
using PaperRendererPtr = fxl::RefPtr<PaperRenderer>;
using PaperScenePtr = fxl::RefPtr<PaperScene>;
using PaperMaterialPtr = MaterialPtr;

// The following types are not relevant to clients who use PaperRenderer only
// with existing subclasses of PaperDrawable, only to clients who implement
// their own subclasses.

// PaperDrawCallFactory generates PaperDrawCalls and enqueues them into a
// PaperRenderQueue.  The number of draw-calls and the precise details of each
// depend on the factory's configuration (e.g. the current shadow algorithm),
// which is controlled by PaperRenderer that owns the factory.
class PaperDrawCallFactory;

// The following types are implementation details of PaperRenderer, which are
// invisible to clients.

// PaperDrawCall encapsulates a RenderQueueItem along with flags that specify
// how it is to be enqueued in a PaperRenderQueue.
struct PaperDrawCall;

// PaperRenderQueue accepts enqueued PaperDrawCalls from PaperDrawCallFactory,
// adding each encapsulated RenderQueueItem to the proper internal RenderQueue.
//
// PaperRenderer first calls Sort() to sort these RenderQueueItems, then calls
// GenerateCommands() to generate Vulkan commands from them.  The latter accepts
// a PaperRenderQueueContext (a subclass of RenderQueueContext); this is passed
// to each RenderQueueItem, and affects the resulting Vulkan commands.
class PaperRenderQueue;
class PaperRenderQueueContext;

// PaperShapeCache is a helper class used by PaperDrawCallFactory.  It caches
// meshes for shapes such as circles and rounded-rectangles.  If a mesh is not
// found then a new one is tessellated, clipped against clip-planes, optionally
// post-processed (e.g. to support extrusion of shadow-volume geometry), then
// uploaded to the GPU.
class PaperShapeCache;
struct PaperShapeCacheEntry;

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_README_H_
