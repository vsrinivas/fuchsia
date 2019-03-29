// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/playback/mediaplayer/test/fakes/fake_session.h"

#include <iomanip>
#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fidl/cpp/optional.h"
#include "src/lib/fxl/logging.h"
#include "src/media/playback/mediaplayer/graph/formatting.h"

namespace media_player {
namespace test {
namespace {

constexpr uint64_t kPresentationRatePerSecond = 60;
constexpr zx::duration kPresentationInterval =
    zx::duration(ZX_SEC(1) / kPresentationRatePerSecond);
constexpr uint32_t kRootNodeId = 1;

}  // namespace

FakeSession::FakeSession()
    : dispatcher_(async_get_default_dispatcher()),
      binding_(this),
      weak_factory_(this) {
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

  PresentScene();
}

void FakeSession::DumpExpectations(uint32_t display_height) {
  if (image_pipe_) {
    image_pipe_->DumpExpectations(display_height);
  } else {
    dump_expectations_ = true;
    expected_display_height_ = display_height;
  }
}

void FakeSession::SetExpectations(
    uint32_t black_image_id, const fuchsia::images::ImageInfo& black_image_info,
    const fuchsia::images::ImageInfo& info, uint32_t display_height,
    const std::vector<PacketInfo>&& expected_packets_info) {
  if (image_pipe_) {
    image_pipe_->SetExpectations(black_image_id, black_image_info, info,
                                 display_height,
                                 std::move(expected_packets_info));
  } else {
    expected_black_image_id_ = black_image_id;
    expected_black_image_info_ = fidl::MakeOptional(black_image_info);
    expected_image_info_ = fidl::MakeOptional(info);
    expected_packets_info_ = std::move(expected_packets_info);
    expected_display_height_ = display_height;
  }
}

void FakeSession::Enqueue(std::vector<::fuchsia::ui::scenic::Command> cmds) {
  for (auto& command : cmds) {
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
                          std::vector<::zx::event> acquire_fences,
                          std::vector<::zx::event> release_fences,
                          PresentCallback callback) {
  // The video renderer doesn't use these fences, so we don't support them in
  // the fake.
  FXL_CHECK(acquire_fences.empty()) << "Present: acquire_fences not supported.";
  FXL_CHECK(release_fences.empty()) << "Present: release_fences not supported.";

  async::PostTask(dispatcher_, [this, callback = std::move(callback),
                                weak_this = GetWeakThis()]() {
    if (!weak_this) {
      return;
    }

    fuchsia::images::PresentationInfo presentation_info;
    presentation_info.presentation_time = next_presentation_time_.get();
    presentation_info.presentation_interval = kPresentationInterval.get();
    callback(presentation_info);
  });
}

void FakeSession::HitTest(uint32_t node_id, ::fuchsia::ui::gfx::vec3 ray_origin,
                          ::fuchsia::ui::gfx::vec3 ray_direction,
                          HitTestCallback callback) {
  FXL_LOG(INFO) << "HitTest (not implemented)";
  // fit::function<void(fidl::VectorPtr<::fuchsia::ui::gfx::Hit>)>
}

void FakeSession::HitTestDeviceRay(::fuchsia::ui::gfx::vec3 ray_origin,
                                   ::fuchsia::ui::gfx::vec3 ray_direction,
                                   HitTestDeviceRayCallback callback) {
  FXL_LOG(INFO) << "HitTestDeviceRay (not implemented)";
  // fit::function<void(fidl::VectorPtr<::fuchsia::ui::gfx::Hit>)>
}

FakeSession::Resource* FakeSession::FindResource(uint32_t id) {
  auto iter = resources_by_id_.find(id);
  return (iter == resources_by_id_.end()) ? nullptr : &iter->second;
}

void FakeSession::HandleCreateResource(uint32_t resource_id,
                                       fuchsia::ui::gfx::ResourceArgs args) {
  if (args.is_image_pipe()) {
    FXL_CHECK(!image_pipe_) << "fake supports only one image pipe.";
    FXL_DCHECK(args.image_pipe().image_pipe_request);
    image_pipe_ = std::make_unique<FakeImagePipe>();
    image_pipe_->Bind(std::move(args.image_pipe().image_pipe_request));
    image_pipe_->OnPresentScene(zx::time(), next_presentation_time_,
                                kPresentationInterval);

    if (dump_expectations_) {
      image_pipe_->DumpExpectations(expected_display_height_);
    }

    if (!expected_packets_info_.empty()) {
      FXL_DCHECK(expected_image_info_);
      image_pipe_->SetExpectations(
          expected_black_image_id_, *expected_black_image_info_,
          *expected_image_info_, expected_display_height_,
          std::move(expected_packets_info_));
    }
  }

  resources_by_id_.emplace(resource_id, std::move(args));
}

void FakeSession::HandleReleaseResource(uint32_t resource_id) {
  if (resources_by_id_.erase(resource_id) != 1) {
    FXL_LOG(ERROR) << "Asked to release unrecognized resource " << resource_id
                   << ", closing connection.";
    expected_ = false;
    binding_.Unbind();
  }
}

fuchsia::ui::gfx::ImagePipeArgs* FakeSession::FindVideoImagePipe(
    uint32_t node_id) {
  Resource* node = FindResource(node_id);
  if (node == nullptr) {
    return nullptr;
  }

  if (node->material_ != kNullResourceId) {
    Resource* material = FindResource(node->material_);
    if (material != nullptr && material->texture_ != kNullResourceId) {
      Resource* texture = FindResource(material->texture_);
      if (texture != nullptr && texture->args_.is_image_pipe()) {
        return &texture->args_.image_pipe();
      }
    }
  }

  for (uint32_t child_id : node->children_) {
    fuchsia::ui::gfx::ImagePipeArgs* result = FindVideoImagePipe(child_id);
    if (result != nullptr) {
      return result;
    }
  }

  return nullptr;
}

void FakeSession::HandleAddChild(uint32_t parent_id, uint32_t child_id) {
  Resource* parent = FindResource(parent_id);
  Resource* child = FindResource(child_id);

  if (!parent) {
    FXL_LOG(ERROR) << "Asked to add child, parent_id (" << parent_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  if (!child) {
    FXL_LOG(ERROR) << "Asked to add child, child_id (" << child_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
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

  if (!node) {
    FXL_LOG(ERROR) << "Asked to set material, node_id (" << node_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  if (!material) {
    FXL_LOG(ERROR) << "Asked to set material, material_id (" << material_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  node->material_ = material_id;
}

void FakeSession::HandleSetTexture(uint32_t material_id, uint32_t texture_id) {
  Resource* material = FindResource(material_id);
  Resource* texture = FindResource(texture_id);

  if (!material) {
    FXL_LOG(ERROR) << "Asked to set texture, material_id (" << material_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  if (!texture) {
    FXL_LOG(ERROR) << "Asked to set texture, texture_id (" << texture_id
                   << ") not recognized, closing connection.";
    expected_ = false;
    binding_.Unbind();
    return;
  }

  material->texture_ = texture_id;
}

void FakeSession::PresentScene() {
  auto now = zx::time(zx_clock_get(ZX_CLOCK_MONOTONIC));

  next_presentation_time_ = now + kPresentationInterval;

  if (image_pipe_) {
    image_pipe_->OnPresentScene(now, next_presentation_time_,
                                kPresentationInterval);
  }

  async::PostTaskForTime(
      dispatcher_,
      [this, weak_this = GetWeakThis()]() {
        if (!weak_this) {
          return;
        }

        PresentScene();
      },
      next_presentation_time_);
}

}  // namespace test
}  // namespace media_player
