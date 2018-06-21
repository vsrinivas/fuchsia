// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/test/fakes/fake_session.h"

#include <iomanip>
#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "garnet/bin/media/media_player/framework/formatting.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fxl/logging.h"

namespace media_player {
namespace test {
namespace {

constexpr uint64_t kPresentationRatePerSecond = 60;
constexpr uint64_t kPresentationInterval =
    ZX_SEC(1) / kPresentationRatePerSecond;
constexpr uint32_t kRootNodeId = 1;

}  // namespace

FakeSession::FakeSession() : async_(async_get_default()), binding_(this) {
  fuchsia::ui::gfx::ResourceArgs root_resource;
  fuchsia::ui::gfx::ViewArgs view_args;
  root_resource.set_view(std::move(view_args));
  resources_by_id_.emplace(kRootNodeId, std::move(root_resource));
}

FakeSession::~FakeSession() {}

void FakeSession::Bind(
    fidl::InterfaceRequest<::fuchsia::ui::scenic::Session> request,
    ::fuchsia::ui::scenic::SessionListenerPtr listener) {
  binding_.Bind(std::move(request));
  listener_ = std::move(listener);
}

void FakeSession::Enqueue(
    fidl::VectorPtr<::fuchsia::ui::scenic::Command> cmds) {
  for (auto& command : *cmds) {
    switch (command.Which()) {
      case fuchsia::ui::scenic::Command::Tag::kGfx:
        switch (command.gfx().Which()) {
          case fuchsia::ui::gfx::Command::Tag::kCreateResource:
            HandleCreateResource(
                command.gfx().create_resource().id,
                std::move(command.gfx().create_resource().resource));
            break;
          case fuchsia::ui::gfx::Command::Tag::kReleaseResource:
            HandleReleaseResource(command.gfx().release_resource().id);
            break;
          case fuchsia::ui::gfx::Command::Tag::kAddChild:
            HandleAddChild(command.gfx().add_child().node_id,
                           command.gfx().add_child().child_id);
            break;
          case fuchsia::ui::gfx::Command::Tag::kSetMaterial:
            HandleSetMaterial(command.gfx().set_material().node_id,
                              command.gfx().set_material().material_id);
            break;
          case fuchsia::ui::gfx::Command::Tag::kSetTexture:
            HandleSetTexture(command.gfx().set_texture().material_id,
                             command.gfx().set_texture().texture_id);
            break;
          default:
            break;
        }
        break;

      case fuchsia::ui::scenic::Command::Tag::kViews:
        FXL_LOG(INFO) << "Enqueue: views (not implemented), tag "
                      << static_cast<fidl_union_tag_t>(command.views().Which());
        break;

      default:
        FXL_LOG(INFO) << "Enqueue: (not implemented), tag "
                      << static_cast<fidl_union_tag_t>(command.Which());
        break;
    }
  }
}

void FakeSession::Present(uint64_t presentation_time,
                          fidl::VectorPtr<::zx::event> acquire_fences,
                          fidl::VectorPtr<::zx::event> release_fences,
                          PresentCallback callback) {
  FXL_DCHECK(acquire_fences->empty());
  FXL_DCHECK(release_fences->empty());

  if (initial_presentation_time_ == 0 && presentation_time != 0) {
    initial_presentation_time_ = presentation_time;
  }

  // Find the video image.
  fuchsia::ui::gfx::ImageArgs* image = FindVideoImage(kRootNodeId);
  if (image != nullptr) {
    if (expected_image_info_) {
      if (image->info.transform != expected_image_info_->transform) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.transform value "
                       << image->info.transform;
        expected_ = false;
      }

      if (image->info.width != expected_image_info_->width) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.width value "
                       << image->info.width;
        expected_ = false;
      }

      if (image->info.height != expected_image_info_->height) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.height value "
                       << image->info.height;
        expected_ = false;
      }

      if (image->info.stride != expected_image_info_->stride) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.stride value "
                       << image->info.stride;
        expected_ = false;
      }

      if (image->info.pixel_format != expected_image_info_->pixel_format) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.pixel_format value "
                       << image->info.pixel_format;
        expected_ = false;
      }

      if (image->info.color_space != expected_image_info_->color_space) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.color_space value "
                       << image->info.color_space;
        expected_ = false;
      }

      if (image->info.tiling != expected_image_info_->tiling) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.tiling value "
                       << image->info.tiling;
        expected_ = false;
      }

      if (image->info.alpha_format != expected_image_info_->alpha_format) {
        FXL_LOG(ERROR) << "unexpected ImageInfo.alpha_format value "
                       << image->info.alpha_format;
        expected_ = false;
      }
    }

    Resource* memory = FindResource(image->memory_id);
    if (memory != nullptr) {
      if (!memory->mapped_buffer_.initialized()) {
        FXL_DCHECK(memory->args_.is_memory());
        FXL_DCHECK(memory->args_.memory().vmo);
        memory->mapped_buffer_.InitFromVmo(
            std::move(memory->args_.memory().vmo), ZX_VM_FLAG_PERM_READ);
      }

      uint64_t size = image->info.stride * image->info.height;

      if (image->memory_offset + size >= memory->mapped_buffer_.size()) {
        FXL_LOG(ERROR) << "image exceeds vmo limits";
        FXL_LOG(ERROR) << "    vmo size     " << memory->mapped_buffer_.size();
        FXL_LOG(ERROR) << "    image offset " << image->memory_offset;
        FXL_LOG(ERROR) << "    image stride " << image->info.stride;
        FXL_LOG(ERROR) << "    image height " << image->info.height;
        expected_ = false;
        return;
      }

      void* image_payload =
          memory->mapped_buffer_.PtrFromOffset(image->memory_offset);

      if (dump_packets_) {
        std::cerr << "{ " << presentation_time - initial_presentation_time_
                  << ", " << size << ", 0x" << std::hex << std::setw(16)
                  << std::setfill('0') << PacketInfo::Hash(image_payload, size)
                  << std::dec << " },\n";
      }

      if (!expected_packets_info_.empty()) {
        if (expected_packets_info_iter_ == expected_packets_info_.end()) {
          FXL_LOG(ERROR) << "frame supplied after expected packets";
          expected_ = false;
        }

        if (expected_packets_info_iter_->timestamp() !=
                static_cast<int64_t>(presentation_time -
                                     initial_presentation_time_) ||
            expected_packets_info_iter_->size() != size ||
            expected_packets_info_iter_->hash() !=
                PacketInfo::Hash(image_payload, size)) {
          FXL_LOG(ERROR) << "supplied frame doesn't match expected packet info";
          FXL_LOG(ERROR) << "actual:   "
                         << presentation_time - initial_presentation_time_
                         << ", " << size << ", 0x" << std::hex << std::setw(16)
                         << std::setfill('0')
                         << PacketInfo::Hash(image_payload, size) << std::dec;
          FXL_LOG(ERROR) << "expected: "
                         << expected_packets_info_iter_->timestamp() << ", "
                         << expected_packets_info_iter_->size() << ", 0x"
                         << std::hex << std::setw(16) << std::setfill('0')
                         << expected_packets_info_iter_->hash() << std::dec;
          expected_ = false;
        }

        ++expected_packets_info_iter_;
      }
    }
  }

  uint64_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
  if (presentation_time < now) {
    presentation_time = now;
  }

  // Round up to the nearest kPresentationInterval.
  presentation_time = (presentation_time + kPresentationInterval - 1);
  presentation_time =
      presentation_time - (presentation_time % kPresentationInterval);

  async::PostTaskForTime(
      async_,
      [this, callback = std::move(callback), presentation_time]() {
        fuchsia::images::PresentationInfo presentation_info;
        presentation_info.presentation_time = presentation_time + ZX_MSEC(20);
        presentation_info.presentation_interval = kPresentationInterval;
        callback(presentation_info);
      },
      zx::time(presentation_time));
}

void FakeSession::HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
                          ::fuchsia::ui::gfx::vec3 ray_direction,
                          HitTestCallback callback) {
  FXL_LOG(INFO) << "HitTest (not implemented)";
}
// fit::function<void(fidl::VectorPtr<::fuchsia::ui::gfx::Hit>)>

void FakeSession::HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                                   ::fuchsia::ui::gfx::vec3 ray_direction,
                                   HitTestDeviceRayCallback callback) {
  FXL_LOG(INFO) << "HitTestDeviceRay (not implemented)";
  // fit::function<void(fidl::VectorPtr<::fuchsia::ui::gfx::Hit>)>
}

FakeSession::Resource* FakeSession::FindResource(uint32_t id) {
  auto iter = resources_by_id_.find(id);
  if (iter == resources_by_id_.end()) {
    FXL_LOG(WARNING) << "Can't find resource " << id;
    expected_ = false;
    return nullptr;
  }

  return &iter->second;
}

void FakeSession::HandleCreateResource(uint32_t resource_id,
                                       fuchsia::ui::gfx::ResourceArgs args) {
  resources_by_id_.emplace(resource_id, std::move(args));
}

void FakeSession::HandleReleaseResource(uint32_t resource_id) {
  if (resources_by_id_.erase(resource_id) != 1) {
    FXL_LOG(WARNING) << "Asked to release unrecognized resource "
                     << resource_id;
    expected_ = false;
  }
}

fuchsia::ui::gfx::ImageArgs* FakeSession::FindVideoImage(uint32_t node_id) {
  Resource* node = FindResource(node_id);
  if (node == nullptr) {
    return nullptr;
  }

  if (node->material_ != kNullResourceId) {
    Resource* material = FindResource(node->material_);
    if (material != nullptr && material->image_texture_) {
      return material->image_texture_.get();
    }
  }

  for (uint32_t child_id : node->children_) {
    fuchsia::ui::gfx::ImageArgs* result = FindVideoImage(child_id);
    if (result != nullptr) {
      return result;
    }
  }

  return nullptr;
}

void FakeSession::HandleAddChild(uint32_t parent_id, uint32_t child_id) {
  Resource* parent = FindResource(parent_id);
  Resource* child = FindResource(child_id);

  if (parent == nullptr || child == nullptr) {
    return;
  }

  if (child->parent_ != kNullResourceId) {
    Resource* prev_parent = FindResource(child->parent_);
    if (prev_parent != nullptr) {
      prev_parent->children_.erase(child_id);
    }
  }

  parent->children_.insert(child_id);
  child->parent_ = parent_id;
}

void FakeSession::HandleSetMaterial(uint32_t node_id, uint32_t material_id) {
  Resource* node = FindResource(node_id);
  Resource* material = FindResource(material_id);

  if (node == nullptr || material == nullptr) {
    return;
  }

  node->material_ = material_id;
}

void FakeSession::HandleSetTexture(uint32_t material_id, uint32_t texture_id) {
  Resource* material = FindResource(material_id);
  Resource* texture = FindResource(texture_id);

  if (material == nullptr || texture == nullptr) {
    return;
  }

  // The texture is released before presentation, so we don't use the resource
  // id.
  if (texture->args_.is_image()) {
    material->image_texture_ = fidl::MakeOptional(texture->args_.image());
  }
}

}  // namespace test
}  // namespace media_player
