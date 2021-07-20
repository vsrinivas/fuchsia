// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/engine.h"

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/trace/event.h>

#include <set>
#include <string>
#include <unordered_set>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/vk/chained_semaphore_generator.h"
#include "src/ui/lib/escher/vk/command_buffer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/compositor.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/dump_visitor.h"
#include "src/ui/scenic/lib/gfx/resources/has_renderable_content_visitor.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/protected_memory_visitor.h"
#include "src/ui/scenic/lib/gfx/swapchain/frame_timings.h"

namespace scenic_impl {
namespace gfx {

Engine::Engine(escher::EscherWeakPtr weak_escher,
               std::shared_ptr<GfxBufferCollectionImporter> buffer_collection_importer,
               inspect::Node inspect_node, RequestFocusFunc request_focus)
    : escher_(std::move(weak_escher)),
      engine_renderer_(std::make_unique<EngineRenderer>(
          escher_,
          escher::ESCHER_CHECKED_VK_RESULT(escher_->device()->caps().GetMatchingDepthStencilFormat(
              {vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint})))),
      view_linker_(ViewLinker::New()),
      image_factory_(std::make_unique<escher::ImageFactoryAdapter>(escher()->gpu_allocator(),
                                                                   escher()->resource_recycler())),
      buffer_collection_importer_(buffer_collection_importer),
      scene_graph_(std::move(request_focus)),
      inspect_node_(std::move(inspect_node)),
      weak_factory_(this) {
  FX_DCHECK(escher_);

  InitializeInspectObjects();
  InitializeAnnotationManager();
}

Engine::Engine(escher::EscherWeakPtr weak_escher)
    : escher_(std::move(weak_escher)),
      view_linker_(ViewLinker::New()),
      image_factory_(escher() ? std::make_unique<escher::ImageFactoryAdapter>(
                                    escher()->gpu_allocator(), escher()->resource_recycler())
                              : nullptr),
      scene_graph_(/*request_focus*/ [](auto...) { return false; }),
      weak_factory_(this) {
  InitializeInspectObjects();
  InitializeAnnotationManager();
}

void Engine::InitializeAnnotationManager() {
  constexpr SessionId kAnnotationSessionId = 0U;
  auto annotation_session = std::make_unique<Session>(kAnnotationSessionId, session_context());
  annotation_manager_ = std::make_unique<AnnotationManager>(scene_graph(), view_linker(),
                                                            std::move(annotation_session));
}

constexpr char kSceneDump[] = "scene_dump";

void Engine::InitializeInspectObjects() {
  inspect_scene_dump_ = inspect_node_.CreateLazyValues(kSceneDump, [this] {
    inspect::Inspector insp;
    if (scene_graph_.compositors().empty()) {
      insp.GetRoot().CreateString(kSceneDump, "(no compositors)", &insp);
    } else {
      std::ostringstream output;
      std::map<GlobalId, std::string> view_debug_names;
      std::map<GlobalId, std::string> view_holder_debug_names;
      output << std::endl;
      for (auto& c : scene_graph_.compositors()) {
        output << "========== BEGIN COMPOSITOR DUMP ======================" << std::endl;
        DumpVisitor visitor(DumpVisitor::VisitorContext(output, nullptr, &view_debug_names,
                                                        &view_holder_debug_names));
        c->Accept(&visitor);
        output << "============ END COMPOSITOR DUMP ======================";
      }
      insp.GetRoot().CreateString(kSceneDump, output.str(), &insp);

      // The debug names of Views/ViewHolders are omitted from the "kSceneDump" string created
      // above, because they may contain PII.  Instead, we write the mappings from
      // View/ViewHolder -> name as separate properties, which can be filtered out when reporting
      // feedback.
      auto view_names = insp.GetRoot().CreateChild("scene_dump_named_views");
      auto view_holder_names = insp.GetRoot().CreateChild("scene_dump_named_view_holders");
      for (auto& pair : view_debug_names) {
        view_names.CreateString(static_cast<std::string>(pair.first), pair.second, &insp);
      }
      for (auto& pair : view_holder_debug_names) {
        view_holder_names.CreateString(static_cast<std::string>(pair.first), pair.second, &insp);
      }
      insp.emplace(std::move(view_names));
      insp.emplace(std::move(view_holder_names));
    }
    return fpromise::make_ok_promise(std::move(insp));
  });
}

void Engine::RenderScheduledFrame(uint64_t frame_number, zx::time presentation_time,
                                  FramePresentedCallback callback) {
  is_rendering_ = true;
  // Because this timings object is passed to the compositor, it may outlive this object. So we
  // capture this weakly, just in case.
  auto timings = std::make_shared<FrameTimings>(
      frame_number,
      /*presented_callback=*/[weak = weak_factory_.GetWeakPtr(),
                              callback = std::move(callback)](const FrameTimings& timings) {
        if (weak) {
          weak->is_rendering_ = false;
        }
        callback(timings.GetTimestamps());
      });

  // NOTE: this name is important for benchmarking.  Do not remove or modify it
  // without also updating the "process_gfx_trace.go" script.
  TRACE_DURATION("gfx", "RenderFrame", "frame_number", frame_number, "time",
                 presentation_time.get());

  TRACE_FLOW_STEP("gfx", "scenic_frame", frame_number);

  UpdateAndDeliverMetrics(presentation_time);

  struct SwapchainLayer {
    Swapchain* swapchain;
    Layer* layer;
  };
  std::vector<SwapchainLayer> swapchain_layers_to_render;

  for (auto& compositor : scene_graph_.compositors()) {
    Swapchain* swapchain = compositor->swapchain();
    if (!swapchain)
      continue;

    Layer* layer = compositor->GetDrawableLayer();
    if (!layer)
      continue;

    // Don't render any initial frames if there is no shapenode with a material
    // in the scene, i.e. anything that could actually be rendered. We do this
    // to avoid triggering any changes in the display swapchain until we have
    // content ready to render.
    if (first_frame_) {
      if (CheckForRenderableContent(*layer)) {
        first_frame_ = false;
      } else {
        continue;
      }
    }

    swapchain_layers_to_render.push_back({swapchain, layer});

    // Verbose logging of the entire Compositor resource tree.
    if (FX_VLOG_IS_ON(3)) {
      std::ostringstream output;
      DumpVisitor visitor(DumpVisitor::VisitorContext(output, nullptr));
      compositor->Accept(&visitor);
      FX_VLOGS(3) << "Compositor dump\n" << output.str();
    }
  }
  if (swapchain_layers_to_render.empty()) {
    // No compositor has any renderable content.
    timings->OnFrameSkipped();
    return;
  }

  // TODO(fxbug.dev/24297): the FrameTimings are passed to the Compositor's swapchain
  // to notify when the frame is finished rendering, presented, dropped, etc.  Although FrameTimings
  // supports specifying the number of swapchains via |RegisterSwapchains(count)|, we haven't fully
  // investigated whether the behavior is suitable in the case of multiple swapchains (e.g. the
  // current policy is to report the |frame_rendered_time| as the latest of all calls to
  // OnFrameRendered(), and similar for the |frame_presented_time|).  Put a CHECK here to make sure
  // that we revisit this, if/when necessary.
  FX_CHECK(swapchain_layers_to_render.size() == 1);
  timings->RegisterSwapchains(swapchain_layers_to_render.size());
  for (size_t i = 0; i < swapchain_layers_to_render.size(); ++i) {
    auto& swapchain = *swapchain_layers_to_render[i].swapchain;
    auto& layer = *swapchain_layers_to_render[i].layer;

    const bool uses_protected_memory = CheckForProtectedMemoryUse(layer);
    if (last_frame_uses_protected_memory_ != uses_protected_memory) {
      swapchain.SetUseProtectedMemory(uses_protected_memory);
      last_frame_uses_protected_memory_ = uses_protected_memory;
    }

    // TODO(fxbug.dev/24297): do we really want to do this once per swapchain?  Or should this be
    // moved outside of the loop?
    if (uses_protected_memory) {
      // NOTE: This name is important for benchmarking.  Do not remove or modify
      // it without also updating tests and benchmarks that depend on it.
      TRACE_INSTANT("gfx", "RenderProtectedFrame", TRACE_SCOPE_THREAD);
    }

    escher::FramePtr frame = escher()->NewFrame("Scenic Compositor", frame_number, false,
                                                escher::CommandBuffer::Type::kGraphics,
                                                uses_protected_memory ? true : false);
    frame->DisableLazyPipelineCreation();

    const bool is_last_layer = (i == swapchain_layers_to_render.size() - 1);
    swapchain.DrawAndPresentFrame(
        timings, i, layer,
        [is_last_layer, &frame, escher{escher_}, engine_renderer{engine_renderer_.get()},
         semaphore_chain{escher_->semaphore_chain()},
         presentation_time](const escher::ImagePtr& output_image, Layer& layer,
                            const escher::SemaphorePtr& acquire_semaphore,
                            const escher::SemaphorePtr& frame_done_semaphore) {
          FX_DCHECK(engine_renderer);
          engine_renderer->RenderLayer(
              frame, presentation_time,
              {.output_image = output_image, .output_image_acquire_semaphore = acquire_semaphore},
              layer);

          // Create a flow event that ends in the magma system driver.
          zx::event semaphore_event = GetEventForSemaphore(escher->device(), frame_done_semaphore);
          zx_info_handle_basic_t info;
          zx_status_t status =
              semaphore_event.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
          ZX_DEBUG_ASSERT(status == ZX_OK);
          TRACE_FLOW_BEGIN("gfx", "semaphore", info.koid);

          if (!is_last_layer) {
            frame->SubmitPartialFrame(frame_done_semaphore);
          } else {
            auto semaphore_pair = semaphore_chain->TakeLastAndCreateNextSemaphore();
            frame->cmds()->AddSignalSemaphore(std::move(semaphore_pair.semaphore_to_signal));
            frame->cmds()->AddWaitSemaphore(std::move(semaphore_pair.semaphore_to_wait),
                                            vk::PipelineStageFlagBits::eVertexInput |
                                                vk::PipelineStageFlagBits::eFragmentShader |
                                                vk::PipelineStageFlagBits::eColorAttachmentOutput |
                                                vk::PipelineStageFlagBits::eTransfer);
            frame->EndFrame(frame_done_semaphore, nullptr);
          }
        });
  }

  timings->OnFrameCpuRendered(async::Now(async_get_default_dispatcher()));
  CleanupEscher();
}

void Engine::SignalFencesWhenPreviousRendersAreDone(std::vector<zx::event> fences) {
  if (fences.empty()) {
    return;
  }

  // TODO(fxbug.dev/24531): Until this bug is fixed, and we perform pipelining in the default frame
  // scheduler, we should never hit this case in production. The code is optimized for when
  // is_rendering_ is false.
  if (is_rendering_) {
    auto cmds =
        escher::CommandBuffer::NewForType(escher_.get(), escher::CommandBuffer::Type::kTransfer,
                                          /* use_protected_memory */ false);
    auto semaphore_pair = escher_->semaphore_chain()->TakeLastAndCreateNextSemaphore();
    cmds->AddWaitSemaphore(std::move(semaphore_pair.semaphore_to_wait),
                           vk::PipelineStageFlagBits::eVertexInput |
                               vk::PipelineStageFlagBits::eFragmentShader |
                               vk::PipelineStageFlagBits::eColorAttachmentOutput |
                               vk::PipelineStageFlagBits::eTransfer);
    cmds->AddSignalSemaphore(std::move(semaphore_pair.semaphore_to_signal));
    for (auto& f : fences) {
      auto semaphore = escher::Semaphore::New(escher_->vk_device());
      vk::ImportSemaphoreZirconHandleInfoFUCHSIA info;
      info.semaphore = semaphore->vk_semaphore();
      info.zirconHandle = f.release();
      info.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eZirconEventFUCHSIA;

      auto result = escher_->vk_device().importSemaphoreZirconHandleFUCHSIA(
          info, escher_->device()->dispatch_loader());
      FX_DCHECK(result == vk::Result::eSuccess);
      cmds->AddSignalSemaphore(std::move(semaphore));
    }
    cmds->Submit(/*callback*/ nullptr);
  } else {
    for (auto& f : fences) {
      f.signal(0u, ZX_EVENT_SIGNALED);
    }
  }
}

bool Engine::CheckForRenderableContent(Layer& layer) {
  TRACE_DURATION("gfx", "CheckForRenderableContent");

  HasRenderableContentVisitor visitor;
  layer.Accept(&visitor);

  return visitor.HasRenderableContent();
}

bool Engine::CheckForProtectedMemoryUse(Layer& layer) {
  TRACE_DURATION("gfx", "CheckForProtectedMemoryUse");

  if (!escher()->allow_protected_memory())
    return false;

  ProtectedMemoryVisitor visitor;
  layer.Accept(&visitor);

  return visitor.HasProtectedMemoryUse();
}

void Engine::UpdateAndDeliverMetrics(zx::time presentation_time) {
  // NOTE: this name is important for benchmarking.  Do not remove or modify it
  // without also updating the "process_gfx_trace.go" script.
  TRACE_DURATION("gfx", "UpdateAndDeliverMetrics", "time", presentation_time.get());

  // Gather all of the scene which might need to be updated.
  std::set<Scene*> scenes;
  for (auto compositor : scene_graph_.compositors()) {
    compositor->CollectScenes(&scenes);
  }
  if (scenes.empty())
    return;

  // TODO(fxbug.dev/23464): Traversing the whole graph just to compute this is pretty
  // inefficient.  We should optimize this.
  fuchsia::ui::gfx::Metrics metrics;
  metrics.scale_x = 1.f;
  metrics.scale_y = 1.f;
  metrics.scale_z = 1.f;
  std::vector<Node*> updated_nodes;
  for (auto scene : scenes) {
    UpdateMetrics(scene, metrics, &updated_nodes);
  }

  // TODO(fxbug.dev/23464): Deliver events to sessions in batches.
  // We probably want delivery to happen somewhere else which can also
  // handle delivery of other kinds of events.  We should probably also
  // have some kind of backpointer from a session to its handler.
  for (auto node : updated_nodes) {
    if (auto event_reporter = node->event_reporter()) {
      fuchsia::ui::gfx::Event event;
      event.set_metrics(::fuchsia::ui::gfx::MetricsEvent());
      event.metrics().node_id = node->id();
      event.metrics().metrics = node->reported_metrics();
      event_reporter->EnqueueEvent(std::move(event));
    }
  }
}

// TODO(mikejurka): move this to appropriate util file
bool MetricsEquals(const fuchsia::ui::gfx::Metrics& a, const fuchsia::ui::gfx::Metrics& b) {
  return a.scale_x == b.scale_x && a.scale_y == b.scale_y && a.scale_z == b.scale_z;
}

void Engine::UpdateMetrics(Node* node, const fuchsia::ui::gfx::Metrics& parent_metrics,
                           std::vector<Node*>* updated_nodes) {
  fuchsia::ui::gfx::Metrics local_metrics;
  local_metrics.scale_x = parent_metrics.scale_x * node->scale().x;
  local_metrics.scale_y = parent_metrics.scale_y * node->scale().y;
  local_metrics.scale_z = parent_metrics.scale_z * node->scale().z;

  if ((node->event_mask() & fuchsia::ui::gfx::kMetricsEventMask) &&
      !MetricsEquals(node->reported_metrics(), local_metrics)) {
    node->set_reported_metrics(local_metrics);
    updated_nodes->push_back(node);
  }

  ForEachChildFrontToBack(*node, [this, &local_metrics, updated_nodes](Node* node) {
    UpdateMetrics(node, local_metrics, updated_nodes);
  });
}

void Engine::CleanupEscher() {
  // Either there is already a cleanup scheduled (meaning that this was already
  // called recently), or there is no Escher because we're running tests.
  if (!escher_ || escher_cleanup_scheduled_) {
    return;
  }
  // Only trace when there is the possibility of doing work.
  TRACE_DURATION("gfx", "Engine::CleanupEscher");

  if (!escher_->Cleanup()) {
    // Wait long enough to give GPU work a chance to finish.
    //
    // NOTE: If this value changes, you should also change the corresponding
    // kCleanupDelay inside timestamp_profiler.h.
    const zx::duration kCleanupDelay = zx::msec(1);

    escher_cleanup_scheduled_ = true;
    async::PostDelayedTask(
        async_get_default_dispatcher(),
        [weak = weak_factory_.GetWeakPtr()] {
          if (weak) {
            // Recursively reschedule if cleanup is incomplete.
            weak->escher_cleanup_scheduled_ = false;
            weak->CleanupEscher();
          }
        },
        kCleanupDelay);
  }
}

void Engine::DumpScenes(std::ostream& output,
                        std::unordered_set<GlobalId, GlobalId::Hash>* visited_resources) const {
  FX_DCHECK(visited_resources);

  // Dump all Compositors and all transitively-reachable Resources.
  // Remember the set of visited resources; the next step will be to dump the
  // unreachable resources.
  output << "Compositors: \n";
  for (auto compositor : scene_graph_.compositors()) {
    DumpVisitor visitor(DumpVisitor::VisitorContext(output, visited_resources));

    compositor->Accept(&visitor);
    output << "\n===\n\n";
  }
}

}  // namespace gfx
}  // namespace scenic_impl
