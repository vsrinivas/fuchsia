// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SIMPLEST_SYSMEM_VIEW_PROVIDER_H_
#define SRC_UI_EXAMPLES_SIMPLEST_SYSMEM_VIEW_PROVIDER_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

namespace sysmem_example {

enum class RenderType {
  PNG,         // Loads a .png file from resource, and render using sysmem.
  RECTANGLE,   // Uses Flagland API `CreateFilledRect` to draw a fuchsia colored rectangle.
  COLOR_BLOCK  // Generates a 4 colored block and render using sysmem.
};

class ViewProviderImpl : fuchsia::ui::app::ViewProvider {
 public:
  ViewProviderImpl(sys::ComponentContext* component_context, sysmem_example::RenderType type);
  ~ViewProviderImpl() override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;

  // |fuchsia::ui::app::ViewProvider|
  void CreateView2(fuchsia::ui::app::CreateView2Args args) override;

 private:
  // Generates a 4 block image based on `image_width` and `image_height`
  //
  // @param image_width Image width in number of pixels.
  // @param image_height Image height in number of pixels.
  // @param image_bytes Array pointer used to stores the generated image pixels.
  void GenerateColorBlockImage(uint32_t image_width, uint32_t image_height, uint8_t* image_bytes);

  // Populates a image buffer with provided arguments and sets up the image for present using
  // Flatland.
  //
  // @param image_data Image vector containing bytes in the specified `pixel_format`.
  // @param image_width Image width in number of pixels.
  // @param image_height Image height in number of pixels.
  // @param pixel_format One of pixel supported pixel format (ex: BGRA32, R8G8B8A8).
  void WriteToSysmem(uint8_t* image_data, uint32_t image_width, uint32_t image_height,
                     fuchsia::sysmem::PixelFormatType pixel_format);

  fidl::BindingSet<fuchsia::ui::app::ViewProvider> bindings_;
  std::optional<fuchsia::ui::composition::LayoutInfo> layout_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  fuchsia::ui::composition::FlatlandPtr flatland_;
  fuchsia::ui::composition::AllocatorSyncPtr flatland_allocator_;
  fuchsia::ui::composition::ParentViewportWatcherPtr parent_watcher_;
  std::unique_ptr<sys::ComponentContext> context_;
  RenderType render_type_;

  static constexpr uint64_t kRootTransformId = 1;

  // Color pixels in BGRA32 format.
  static constexpr uint8_t kRed[] = {0, 0, 255, 255};
  static constexpr uint8_t kGreen[] = {0, 255, 0, 255};
  static constexpr uint8_t kBlue[] = {255, 0, 0, 255};
  static constexpr uint8_t kYellow[] = {0, 255, 255, 255};
};

}  // namespace sysmem_example

#endif  // SRC_UI_EXAMPLES_SIMPLEST_SYSMEM_VIEW_PROVIDER_H_
