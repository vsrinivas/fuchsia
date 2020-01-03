// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/images/cpp/images.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <math.h>

#include <cmath>

#include <trace-provider/provider.h>

#include "src/lib/fxl/logging.h"

namespace {
const float FRAMEDROP_DETECTION_FACTOR = 1.2f;
const size_t SIZE_OF_BGRA8 = sizeof(uint32_t);
}  // namespace

class View : public fuchsia::ui::scenic::SessionListener {
 public:
  View(sys::ComponentContext* component_context, fuchsia::ui::views::ViewToken view_token)
      : session_listener_binding_(this) {
    // Connect to Scenic.
    fuchsia::ui::scenic::ScenicPtr scenic =
        component_context->svc()->Connect<fuchsia::ui::scenic::Scenic>();

    // Create a Scenic Session and a Scenic SessionListener.
    scenic->CreateSession(session_.NewRequest(), session_listener_binding_.NewBinding());

    InitializeScene(std::move(view_token));
  }

 private:
  static void PushCommand(std::vector<fuchsia::ui::scenic::Command>* cmds,
                          fuchsia::ui::gfx::Command cmd) {
    // Wrap the gfx::Command in a scenic::Command, then push it.
    cmds->push_back(scenic::NewCommand(std::move(cmd)));
  }

  void SetBgra8Pixels(uint8_t* vmo_base, fuchsia::images::ImageInfo info) {
    // Set the entire texture to a random bitmap.
    for (uint32_t i = 0; i < info.height * info.width * SIZE_OF_BGRA8; ++i) {
      vmo_base[i] = std::rand() % std::numeric_limits<uint8_t>::max();
    }
  }

  uint32_t CreateTexture(uint32_t width, uint32_t height) {
    static const auto kFormat = fuchsia::images::PixelFormat::BGRA_8;
    fuchsia::images::ImageInfo image_info{
        .width = width,
        .height = height,
        .stride = static_cast<uint32_t>(width * images::StrideBytesPerWidthPixel(kFormat)),
        .pixel_format = kFormat,
    };

    uint64_t image_vmo_bytes = images::ImageSize(image_info);

    zx::vmo image_vmo;
    zx_status_t status = zx::vmo::create(image_vmo_bytes, 0, &image_vmo);
    if (status != ZX_OK) {
      FXL_LOG(FATAL) << "::zx::vmo::create() failed";
      FXL_NOTREACHED();
    }

    uint8_t* vmo_base;
    status = zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                        ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                        reinterpret_cast<uintptr_t*>(&vmo_base));

    SetBgra8Pixels(vmo_base, image_info);

    std::vector<fuchsia::ui::scenic::Command> cmds;

    uint32_t memory_id = new_resource_id_++;
    PushCommand(&cmds, scenic::NewCreateMemoryCmd(memory_id, std::move(image_vmo), image_vmo_bytes,
                                                  fuchsia::images::MemoryType::HOST_MEMORY));
    uint32_t image_id = new_resource_id_++;
    PushCommand(&cmds, scenic::NewCreateImageCmd(image_id, memory_id, 0, image_info));

    session_->Enqueue(std::move(cmds));
    return image_id;
  }

  void InitializeScene(fuchsia::ui::views::ViewToken view_token) {
    // Build up a list of commands we will send over our Scenic Session.
    std::vector<fuchsia::ui::scenic::Command> cmds;

    // View: Use |view_token| to create a View in the Session.
    PushCommand(&cmds, scenic::NewCreateViewCmd(kViewId, std::move(view_token),
                                                "transparency_benchmark_view"));
    PushCommand(&cmds, scenic::NewCreateEntityNodeCmd(kScaleId));
    PushCommand(&cmds, scenic::NewAddChildCmd(kViewId, kScaleId));

    for (int i = 0; i < kFullScreenLayers; i++) {
      int shape_id = new_resource_id_++;
      PushCommand(&cmds, scenic::NewCreateShapeNodeCmd(shape_id));
      full_screen_shape_nodes_.push_back(shape_id);

      PushCommand(&cmds, scenic::NewAddChildCmd(kScaleId, shape_id));

      //  Material.
      int material_id = new_resource_id_++;
      PushCommand(&cmds, scenic::NewCreateMaterialCmd(material_id));
      PushCommand(&cmds, scenic::NewSetMaterialCmd(shape_id, material_id));
      shape_node_materials_.push_back(material_id);
    }

    session_->Enqueue(std::move(cmds));

    // Apply all the commands we've enqueued by calling Present. For this first
    // frame we call Present with a presentation_time = 0 which means it the
    // commands should be applied immediately. For future frames, we'll use the
    // timing information we receive to have precise presentation times.
    session_->Present(
        0, {}, {}, [this](fuchsia::images::PresentationInfo info) { OnPresent(std::move(info)); });
  }

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicError(std::string error) override { FXL_LOG(INFO) << "ERROR: " << error; }

  static bool IsViewAttachedToSceneEvent(const fuchsia::ui::scenic::Event& event) {
    return event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
           event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewAttachedToScene;
  }

  static bool IsViewPropertiesChangedEvent(const fuchsia::ui::scenic::Event& event) {
    return event.Which() == fuchsia::ui::scenic::Event::Tag::kGfx &&
           event.gfx().Which() == fuchsia::ui::gfx::Event::Tag::kViewPropertiesChanged;
  }

  static bool IsPointerEvent(const fuchsia::ui::scenic::Event& event) {
    return event.Which() == fuchsia::ui::scenic::Event::Tag::kInput &&
           event.input().Which() == fuchsia::ui::input::InputEvent::Tag::kPointer;
  }

  static bool IsPointerDownEvent(const fuchsia::ui::scenic::Event& event) {
    return IsPointerEvent(event) &&
           event.input().pointer().phase == fuchsia::ui::input::PointerEventPhase::DOWN;
  }

  static bool IsPointerUpEvent(const fuchsia::ui::scenic::Event& event) {
    return IsPointerEvent(event) &&
           event.input().pointer().phase == fuchsia::ui::input::PointerEventPhase::UP;
  }

  bool attached_ = false;
  bool sized_ = false;

  // |fuchsia::ui::scenic::SessionListener|
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events) override {
    for (auto& event : events) {
      if (IsViewAttachedToSceneEvent(event)) {
        attached_ = true;
      }
      if (IsViewPropertiesChangedEvent(event)) {
        OnViewPropertiesChanged(event.gfx().view_properties_changed().properties);
        sized_ = true;
      } else if (IsPointerDownEvent(event)) {
        pointer_down_ = true;
        pointer_id_ = event.input().pointer().pointer_id;
      } else if (IsPointerUpEvent(event)) {
        if (pointer_id_ == event.input().pointer().pointer_id) {
          pointer_down_ = false;
        }
      } else {
        // Unhandled event.
      }
    }
  }

  void OnViewPropertiesChanged(fuchsia::ui::gfx::ViewProperties vp) {
    view_width_ = (vp.bounding_box.max.x - vp.inset_from_max.x) -
                  (vp.bounding_box.min.x + vp.inset_from_min.x);
    view_height_ = (vp.bounding_box.max.y - vp.inset_from_max.y) -
                   (vp.bounding_box.min.y + vp.inset_from_min.y);

    FXL_LOG(INFO) << "OnViewPropertiesChanged " << view_width_ << " " << view_height_;

    if (view_width_ == 0 || view_height_ == 0)
      return;

    // Build up a list of commands we will send over our Scenic Session.
    std::vector<fuchsia::ui::scenic::Command> cmds;

    float offset = 1.0f;
    for (auto shape_node_id : full_screen_shape_nodes_) {
      int rectangle_id = new_resource_id_++;
      PushCommand(&cmds, scenic::NewCreateRectangleCmd(rectangle_id, view_width_, view_height_));
      PushCommand(&cmds, scenic::NewSetShapeCmd(shape_node_id, rectangle_id));
      offset += 1.0f;
    }

    for (int i = 0; i < kFullScreenLayers; i++) {
      full_res_textures_.push_back(CreateTexture(view_width_, view_height_));
    }

    state_ = BLANK;
    InitBlank(&cmds);
    session_->Enqueue(std::move(cmds));

    // The commands won't actually get committed until Session.Present() is
    // called. However, since we're animating every frame, in this case we can
    // assume Present() will be called shortly.
  }

  enum State {
    INIT = -1,
    BLANK = 0,
    SOLID = 1,
    SOLID_WITH_TEXTURE = 2,
    ALPHA = 3,
    ALPHA_WITH_SAME_TEXTURE = 4,
    ALPHA_WITH_SEPARATE_TEXTURES = 5,
    NUM_STATES = 6,
  };

  State state_ = INIT;
  std::string state_names_[NUM_STATES] = {"BLANK",
                                          "SOLID",
                                          "SOLID_WITH_TEXTURE",
                                          "ALPHA",
                                          "ALPHA_WITH_SAME_TEXTURE",
                                          "ALPHA_WITH_SEPARATE_TEXTURES"};

  void DetachAll(std::vector<fuchsia::ui::scenic::Command>* cmds) {
    for (auto id : full_screen_shape_nodes_) {
      PushCommand(cmds, scenic::NewDetachCmd(id));
    }
  }

  void Tile(std::vector<fuchsia::ui::scenic::Command>* cmds, int width, int height, int depth) {
    // Position is relative to the View's origin system.
    const float center_x = view_width_ * .5f;
    const float center_y = view_height_ * .5f;
    for (int i = 0; i < kFullScreenLayers; i++) {
      float x = i % width;
      float y = (i / width) % height;
      float z = i / (width * height);
      auto id = full_screen_shape_nodes_[i];

      static const float PI = 3.14159;

      PushCommand(cmds,
                  scenic::NewSetRotationCmd(id, {0, 0, std::sin(PI / 2.0f), std::cos(PI / 2.0f)}));
      PushCommand(cmds, scenic::NewSetTranslationCmd(id, {center_x + x * view_width_,
                                                          center_y + y * view_height_, z * -1.0f}));
    }
  }

  void InitBlank(std::vector<fuchsia::ui::scenic::Command>* cmds) { DetachAll(cmds); }

  bool Blank(std::vector<fuchsia::ui::scenic::Command>* cmds, int level) {
    return level >= kFullScreenLayers;
  }

  void InitSolid(std::vector<fuchsia::ui::scenic::Command>* cmds) {
    DetachAll(cmds);
    Tile(cmds, 1, 1, kFullScreenLayers);
    for (auto id : shape_node_materials_) {
      PushCommand(cmds, scenic::NewSetTextureCmd(id, 0));
      PushCommand(cmds, scenic::NewSetColorCmd(id, 0xff, 0xff, 0xff, 0xff));
    }
  }

  void InitSolidWithTexture(std::vector<fuchsia::ui::scenic::Command>* cmds) {
    DetachAll(cmds);
    Tile(cmds, 1, 1, kFullScreenLayers);
    for (int i = 0; i < kFullScreenLayers; i++) {
      PushCommand(cmds, scenic::NewSetTextureCmd(shape_node_materials_[i], full_res_textures_[i]));
      PushCommand(cmds, scenic::NewSetColorCmd(shape_node_materials_[i], 0xff, 0xff, 0xff, 0xff));
    }
  }

  void InitAlpha(std::vector<fuchsia::ui::scenic::Command>* cmds) {
    DetachAll(cmds);
    Tile(cmds, 1, 1, kFullScreenLayers);
    for (auto id : shape_node_materials_) {
      PushCommand(cmds, scenic::NewSetTextureCmd(id, 0));
      PushCommand(cmds, scenic::NewSetColorCmd(id, 0xff, 0xff, 0xff, 0x80));
    }
  }

  void InitAlphaWithSameTexture(std::vector<fuchsia::ui::scenic::Command>* cmds) {
    DetachAll(cmds);
    Tile(cmds, 1, 1, kFullScreenLayers);
    for (int i = 0; i < kFullScreenLayers; i++) {
      PushCommand(cmds, scenic::NewSetTextureCmd(shape_node_materials_[i], full_res_textures_[0]));
      PushCommand(cmds, scenic::NewSetColorCmd(shape_node_materials_[i], 0xff, 0xff, 0xff, 0x80));
    }
  }

  void InitAlphaWithSeparateTextures(std::vector<fuchsia::ui::scenic::Command>* cmds) {
    DetachAll(cmds);
    Tile(cmds, 1, 1, kFullScreenLayers);
    for (int i = 0; i < kFullScreenLayers; i++) {
      PushCommand(cmds, scenic::NewSetTextureCmd(shape_node_materials_[i], full_res_textures_[i]));
      PushCommand(cmds, scenic::NewSetColorCmd(shape_node_materials_[i], 0xff, 0xff, 0xff, 0x80));
    }
  }

  bool LayerUpdate(std::vector<fuchsia::ui::scenic::Command>* cmds, int level) {
    for (int i = 0; i < level; i++) {
      auto id = full_screen_shape_nodes_[i];
      PushCommand(cmds, scenic::NewDetachCmd(id));
      PushCommand(cmds, scenic::NewAddChildCmd(kScaleId, id));
    }

    return level >= kFullScreenLayers;
  }

  void OnPresent(fuchsia::images::PresentationInfo presentation_info) {
    uint64_t presentation_time = presentation_info.presentation_time;
    constexpr float kSecondsPerNanosecond = .000'000'001f;

    float t = (presentation_time - last_presentation_time_) * kSecondsPerNanosecond;
    if (last_presentation_time_ == 0) {
      t = 0;
    }

    last_presentation_time_ = presentation_time;

    bool done = false;
    std::vector<fuchsia::ui::scenic::Command> cmds;
    switch (state_) {
      case INIT:
        break;
      case BLANK:
        done |= Blank(&cmds, level_);
        break;
      default:
        done |= LayerUpdate(&cmds, level_);
        break;
    }

    if (sample_ > kWarmUpPeriod) {
      avg_times_[sample_ - kWarmUpPeriod] = t;
    }

    ++sample_;

    if (sample_ >= kWarmUpPeriod + kSamples) {
      if (state_ != INIT) {
        float time = 0.0f;
        for (int i = 0; i < kSamples; i++) {
          time += avg_times_[i];
          avg_times_[i] = 0.0f;
        }
        time = time / kSamples;
        FXL_LOG(INFO) << "Tested " << state_names_[state_] << " " << level_
                      << ", avg time: " << time;
        saved_times_[state_] = time;
        saved_levels_[state_] = level_;
        done = saved_times_[state_] > FRAMEDROP_DETECTION_FACTOR * saved_times_[BLANK];
      }
      sample_ = 0;
      level_++;
    }

    if (done) {
      sample_ = 0;
      level_ = 0;

      switch (state_) {
        case BLANK:
          state_ = SOLID;
          InitSolid(&cmds);
          break;
        case SOLID:
          state_ = SOLID_WITH_TEXTURE;
          InitSolidWithTexture(&cmds);
          break;
        case SOLID_WITH_TEXTURE:
          state_ = ALPHA;
          InitAlpha(&cmds);
          break;
        case ALPHA:
          state_ = ALPHA_WITH_SAME_TEXTURE;
          InitAlphaWithSameTexture(&cmds);
          break;
        case ALPHA_WITH_SAME_TEXTURE:
          state_ = ALPHA_WITH_SEPARATE_TEXTURES;
          InitAlphaWithSeparateTextures(&cmds);
          break;
        case ALPHA_WITH_SEPARATE_TEXTURES:
          PrintReport();
          state_ = BLANK;
          InitBlank(&cmds);
          break;
        default:
          break;
      }
    }

    session_->Enqueue(std::move(cmds));

    zx_time_t next_presentation_time = presentation_info.presentation_time + 1;
    session_->Present(
        next_presentation_time, {}, {},
        [this](fuchsia::images::PresentationInfo info) { OnPresent(std::move(info)); });
  }

  void PrintReport() {
    FXL_LOG(INFO) << "----- REPORT -----";
    for (int i = 0; i < NUM_STATES; i++) {
      if (saved_levels_[i] == kFullScreenLayers - 1) {
        FXL_LOG(INFO) << "State " << state_names_[i] << " completed with a running time of "
                      << saved_times_[i];
      } else {
        FXL_LOG(INFO) << "State " << state_names_[i] << " failed at level " << saved_levels_[i]
                      << " with a running time of " << saved_times_[i];
      }
    }
    FXL_LOG(INFO) << "--- END REPORT ---";
  }

  static const int kWarmUpPeriod = 10;
  static const int kSamples = 10;
  int sample_ = 0;
  int level_ = false;
  float avg_times_[kSamples];
  float saved_times_[NUM_STATES];
  int saved_levels_[NUM_STATES];

  const int kViewId = 1;
  const int kScaleId = 2;

  // For other resources we create, we use |new_resource_id_| and then
  // increment it.
  int new_resource_id_ = 3;

  uint64_t last_presentation_time_ = 0;

  float view_width_ = 0;
  float view_height_ = 0;

  static const int kFullScreenLayers = 20;
  std::vector<int> full_screen_shape_nodes_;
  std::vector<int> shape_node_materials_;
  std::vector<int> full_res_textures_;

  // Input.
  bool pointer_down_ = false;
  uint32_t pointer_id_ = 0;

  fidl::Binding<fuchsia::ui::scenic::SessionListener> session_listener_binding_;
  fuchsia::ui::scenic::SessionPtr session_;
};

// Implement the ViewProvider interface, a standard way for an embedder to
// provide us a token that, using Scenic APIs, allows us to create a View
// that's attached to the embedder's ViewHolder.
class ViewProviderService : public fuchsia::ui::app::ViewProvider {
 public:
  ViewProviderService(sys::ComponentContext* component_context)
      : component_context_(component_context) {}

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override {
    auto view =
        std::make_unique<View>(component_context_, scenic::ToViewToken(std::move(view_token)));
    views_.push_back(std::move(view));
  }

  void HandleViewProviderRequest(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  sys::ComponentContext* component_context_ = nullptr;
  std::vector<std::unique_ptr<View>> views_;
  fidl::BindingSet<ViewProvider> bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());

  std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create();

  ViewProviderService view_provider(component_context.get());

  // Add our ViewProvider service to the outgoing services.
  component_context->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      [&view_provider](fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request) {
        view_provider.HandleViewProviderRequest(std::move(request));
      });

  loop.Run();
  return 0;
}
