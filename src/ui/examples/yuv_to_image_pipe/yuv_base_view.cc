// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/yuv_to_image_pipe/yuv_base_view.h"

#include <lib/fdio/directory.h>
#include <lib/images/cpp/images.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <iostream>

#include "src/ui/lib/yuv/yuv.h"

namespace yuv_to_image_pipe {

namespace {

constexpr uint32_t kShapeWidth = 640;
constexpr uint32_t kShapeHeight = 480;
constexpr float kDisplayHeight = 50;
constexpr float kInitialWindowXPos = 320;
constexpr float kInitialWindowYPos = 240;

fuchsia::sysmem::ColorSpaceType DefaultColorSpaceForPixelFormat(
    fuchsia::sysmem::PixelFormatType pixel_format) {
  switch (pixel_format) {
    case fuchsia::sysmem::PixelFormatType::NV12:
    case fuchsia::sysmem::PixelFormatType::I420:
      return fuchsia::sysmem::ColorSpaceType::REC709;
    case fuchsia::sysmem::PixelFormatType::BGRA32:
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return fuchsia::sysmem::ColorSpaceType::SRGB;
    default:
      FX_NOTREACHED() << "Pixel format not supported.";
  }
  return fuchsia::sysmem::ColorSpaceType::INVALID;
}

uint32_t StrideBytesPerWidthPixel(fuchsia::sysmem::PixelFormatType pixel_format) {
  switch (pixel_format) {
    case fuchsia::sysmem::PixelFormatType::NV12:
    case fuchsia::sysmem::PixelFormatType::I420:
      return 1u;
    case fuchsia::sysmem::PixelFormatType::BGRA32:
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return 4u;
    default:
      FX_NOTREACHED() << "Pixel format not supported.";
  }
  return 0;
}

}  // namespace

YuvBaseView::YuvBaseView(scenic::ViewContext context, fuchsia::sysmem::PixelFormatType pixel_format)
    : BaseView(std::move(context), "YuvBaseView Example"),
      node_(session()),
      pixel_format_(pixel_format),
      stride_(static_cast<uint32_t>(kShapeWidth * StrideBytesPerWidthPixel(pixel_format_))) {
  FX_VLOGS(4) << "Creating View";

  // Create an ImagePipe and use it.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic::NewCreateImagePipe2Cmd(image_pipe_id, image_pipe_.NewRequest()));
  // Make sure that |image_pipe_| is created by flushing the enqueued calls.
  session()->Present(0, [](fuchsia::images::PresentationInfo info) {});

  // Create a material that has our image pipe mapped onto it:
  scenic::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rectangle shape to display the YUV on.
  scenic::Rectangle shape(session(), kShapeWidth, kShapeHeight);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  root_node().AddChild(node_);

  // Translation of 0, 0 is the middle of the screen
  node_.SetTranslation(kInitialWindowXPos, kInitialWindowYPos, -kDisplayHeight);
  InvalidateScene();

  zx_status_t status = component_context()->svc()->Connect(sysmem_allocator_.NewRequest());
  FX_CHECK(status == ZX_OK);
}

uint32_t YuvBaseView::AddImage() {
  ++next_image_id_;

  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  FX_CHECK(status == ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr scenic_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), scenic_token.NewRequest());
  FX_CHECK(status == ZX_OK);
  status = local_token->Sync();
  FX_CHECK(status == ZX_OK);

  // Use |next_image_id_| as buffer_id.
  image_pipe_->AddBufferCollection(next_image_id_, std::move(scenic_token));

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                   buffer_collection.NewRequest());
  FX_CHECK(status == ZX_OK);

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = 1;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWrite | fuchsia::sysmem::cpuUsageWriteOften;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.physically_contiguous_required = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.image_format_constraints_count = 1;
  fuchsia::sysmem::ImageFormatConstraints& image_constraints =
      constraints.image_format_constraints[0];
  image_constraints = fuchsia::sysmem::ImageFormatConstraints();
  image_constraints.required_min_coded_width = kShapeWidth;
  image_constraints.required_min_coded_height = kShapeHeight;
  image_constraints.required_max_coded_width = kShapeWidth;
  image_constraints.required_max_coded_height = kShapeHeight;
  image_constraints.required_min_bytes_per_row = stride_;
  image_constraints.required_max_bytes_per_row = stride_;
  image_constraints.pixel_format.type = pixel_format_;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0].type = DefaultColorSpaceForPixelFormat(pixel_format_);
  status = buffer_collection->SetConstraints(true, constraints);
  FX_CHECK(status == ZX_OK);

  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  FX_CHECK(status == ZX_OK);
  FX_CHECK(allocation_status == ZX_OK);
  FX_CHECK(buffer_collection_info.buffers[0].vmo != ZX_HANDLE_INVALID);
  FX_CHECK(buffer_collection_info.settings.image_format_constraints.pixel_format.type ==
           image_constraints.pixel_format.type);
  const bool needs_flush = buffer_collection_info.settings.buffer_settings.coherency_domain ==
                           fuchsia::sysmem::CoherencyDomain::RAM;

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kShapeWidth;
  image_format.coded_height = kShapeHeight;
  image_pipe_->AddImage(next_image_id_, next_image_id_, 0, image_format);
  FX_CHECK(allocation_status == ZX_OK);

  uint8_t* vmo_base;
  const zx::vmo& image_vmo = buffer_collection_info.buffers[0].vmo;
  auto image_vmo_bytes = buffer_collection_info.settings.buffer_settings.size_bytes;
  FX_CHECK(image_vmo_bytes > 0);
  status = zx::vmar::root_self()->map(0, image_vmo, 0, image_vmo_bytes,
                                      ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                      reinterpret_cast<uintptr_t*>(&vmo_base));
  vmo_base += buffer_collection_info.buffers[0].vmo_usable_start;

  image_vmos_.emplace(std::piecewise_construct, std::forward_as_tuple(next_image_id_),
                      std::forward_as_tuple(vmo_base, image_vmo_bytes, needs_flush));

  buffer_collection->Close();
  return next_image_id_;
}

void YuvBaseView::PaintImage(uint32_t image_id, uint8_t pixel_multiplier) {
  FX_CHECK(image_vmos_.count(image_id));

  const ImageVmo& image_vmo = image_vmos_.find(image_id)->second;
  SetVmoPixels(image_vmo.vmo_ptr, pixel_multiplier);

  if (image_vmo.needs_flush) {
    zx_cache_flush(image_vmo.vmo_ptr, image_vmo.image_bytes, ZX_CACHE_FLUSH_DATA);
  }
}

void YuvBaseView::PresentImage(uint32_t image_id) {
  FX_CHECK(image_vmos_.count(image_id));
  TRACE_DURATION("gfx", "YuvBaseView::PresentImage");

  std::vector<zx::event> acquire_fences;
  std::vector<zx::event> release_fences;
  uint64_t now_ns = zx_clock_get_monotonic();
  TRACE_FLOW_BEGIN("gfx", "image_pipe_present_image", image_id);
  image_pipe_->PresentImage(image_id, now_ns, std::move(acquire_fences), std::move(release_fences),
                            [](fuchsia::images::PresentationInfo presentation_info) {
                              std::cout << "PresentImageCallback() called" << std::endl;
                            });
}

void YuvBaseView::SetVmoPixels(uint8_t* vmo_base, uint8_t pixel_multiplier) {
  switch (pixel_format_) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      SetBgra32Pixels(vmo_base, pixel_multiplier);
      break;
    case fuchsia::sysmem::PixelFormatType::I420:
      SetI420Pixels(vmo_base, pixel_multiplier);
      break;
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      SetRgba32Pixels(vmo_base, pixel_multiplier);
      break;
    case fuchsia::sysmem::PixelFormatType::NV12:
      SetNv12Pixels(vmo_base, pixel_multiplier);
      break;
    default:
      FX_NOTREACHED() << "Pixel format not supported.";
  }
}

void YuvBaseView::SetBgra32Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier) {
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter++) {
      double x = static_cast<double>(x_iter) / kShapeWidth;
      uint8_t y_value = static_cast<uint8_t>(GetYValue(x, y) * pixel_multiplier);
      uint8_t u_value = static_cast<uint8_t>(GetUValue(x, y) * pixel_multiplier);
      uint8_t v_value = static_cast<uint8_t>(GetVValue(x, y) * pixel_multiplier);
      yuv::YuvToBgra(y_value, u_value, v_value,
                     &vmo_base[y_iter * stride_ + x_iter * sizeof(uint32_t)]);
    }
  }
}

void YuvBaseView::SetRgba32Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier) {
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter++) {
      double x = static_cast<double>(x_iter) / kShapeWidth;
      uint8_t y_value = static_cast<uint8_t>(GetYValue(x, y) * pixel_multiplier);
      uint8_t u_value = static_cast<uint8_t>(GetUValue(x, y) * pixel_multiplier);
      uint8_t v_value = static_cast<uint8_t>(GetVValue(x, y) * pixel_multiplier);
      uint8_t bgra_val[4];
      yuv::YuvToBgra(y_value, u_value, v_value, bgra_val);
      uint8_t* target = &vmo_base[y_iter * stride_ + x_iter * sizeof(uint32_t)];
      target[0] = bgra_val[2];
      target[1] = bgra_val[1];
      target[2] = bgra_val[0];
      target[3] = bgra_val[3];
    }
  }
}

void YuvBaseView::SetNv12Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier) {
  // Y plane
  uint8_t* y_base = vmo_base;
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter++) {
      double x = static_cast<double>(x_iter) / kShapeWidth;
      y_base[y_iter * stride_ + x_iter] = static_cast<uint8_t>(GetYValue(x, y) * pixel_multiplier);
    }
  }
  // UV interleaved
  uint8_t* uv_base = y_base + kShapeHeight * stride_;
  for (uint32_t y_iter = 0; y_iter < kShapeHeight / 2; y_iter++) {
    double y = static_cast<double>(y_iter * 2) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth / 2; x_iter++) {
      double x = static_cast<double>(x_iter * 2) / kShapeWidth;
      uv_base[y_iter * stride_ + x_iter * 2] =
          static_cast<uint8_t>(GetUValue(x, y) * pixel_multiplier);
      uv_base[y_iter * stride_ + x_iter * 2 + 1] =
          static_cast<uint8_t>(GetVValue(x, y) * pixel_multiplier);
    }
  }
}

void YuvBaseView::SetI420Pixels(uint8_t* vmo_base, uint8_t pixel_multiplier) {
  // Y plane
  uint8_t* y_base = vmo_base;
  for (uint32_t y_iter = 0; y_iter < kShapeHeight; y_iter++) {
    double y = static_cast<double>(y_iter) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth; x_iter++) {
      double x = static_cast<double>(x_iter) / kShapeWidth;
      y_base[y_iter * stride_ + x_iter] = static_cast<uint8_t>(GetYValue(x, y) * pixel_multiplier);
    }
  }
  // U and V work the same as each other, so do them together
  uint8_t* u_base = y_base + kShapeHeight * stride_;
  uint8_t* v_base = u_base + kShapeHeight / 2 * stride_ / 2;
  for (uint32_t y_iter = 0; y_iter < kShapeHeight / 2; y_iter++) {
    double y = static_cast<double>(y_iter * 2) / kShapeHeight;
    for (uint32_t x_iter = 0; x_iter < kShapeWidth / 2; x_iter++) {
      double x = static_cast<double>(x_iter * 2) / kShapeWidth;
      u_base[y_iter * stride_ / 2 + x_iter] =
          static_cast<uint8_t>(GetUValue(x, y) * pixel_multiplier);
      v_base[y_iter * stride_ / 2 + x_iter] =
          static_cast<uint8_t>(GetVValue(x, y) * pixel_multiplier);
    }
  }
}

double YuvBaseView::GetYValue(double x, double y) { return x; }

double YuvBaseView::GetUValue(double x, double y) { return y; }

double YuvBaseView::GetVValue(double x, double y) { return 1 - y; }

}  // namespace yuv_to_image_pipe
