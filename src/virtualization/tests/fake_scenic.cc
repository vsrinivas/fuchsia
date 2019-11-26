// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/fake_scenic.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/virtualization/hardware/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/defer.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <lib/fostr/fidl/fuchsia/ui/scenic/formatting.h>
#include <lib/sys/cpp/service_directory.h>
#include <string.h>
#include <zircon/errors.h>

#include <cstdint>
#include <fstream>
#include <iostream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

// If true, print to log files received Session commands.
constexpr bool kTraceCommands = false;

using fuchsia::ui::input::KeyboardEventPhase;
using fuchsia::ui::scenic::Command;
using fuchsia::ui::scenic::Event;
using fuchsia::ui::scenic::Scenic;
using fuchsia::ui::scenic::Session;
using fuchsia::ui::scenic::SessionListener;
using GfxCommand = fuchsia::ui::gfx::Command;
using fuchsia::ui::gfx::ResourceArgs;

FakeSession::FakeSession(fidl::InterfaceRequest<Session> request,
                         fidl::InterfaceHandle<SessionListener> listener)
    : binding_(this, std::move(request)) {
  listener_.Bind(std::move(listener));
}

void FakeSession::Present(uint64_t /*presentation_time*/,
                          std::vector<::zx::event> /*acquire_fences*/,
                          std::vector<::zx::event> /*release_fences*/, PresentCallback callback) {
  callback({});
}

void FakeSession::HandleGfxCommand(GfxCommand cmd) {
  switch (cmd.Which()) {
    case GfxCommand::Tag::kCreateResource:
      HandleGfxCreateResource(std::move(cmd.create_resource()));
      break;
    case GfxCommand::Tag::kReleaseResource:
      HandleGfxReleaseResource(cmd.release_resource());
      break;
    case GfxCommand::Tag::kSetEventMask:
      HandleSetEventMask(cmd.set_event_mask());
      break;

    default:
      break;
  }
}

void FakeSession::HandleCreateView(uint32_t id) {
  // When a View is created, we need to send a "ViewProperties" describing
  // how large the view is.
  fuchsia::ui::gfx::Event response;
  auto& changed_event = response.view_properties_changed();
  changed_event.view_id = id;
  changed_event.properties.bounding_box.min = {0.0F, 0.0F, 0.0F};
  changed_event.properties.bounding_box.max = {kScreenWidthPixels, kScreenHeightPixels, 1.0F};
  SendGfxEvent(std::move(response));
}

void FakeSession::HandleGfxCreateResource(fuchsia::ui::gfx::CreateResourceCmd cmd) {
  const uint32_t id = cmd.id;
  const ResourceArgs::Tag type = cmd.resource.Which();

  // Track the resource, ensuring another resource with the same
  // ID doesn't already exist.
  auto [_, inserted] = resources_.insert({id, std::move(cmd)});
  FXL_CHECK(inserted) << "Resource ID " << id << " already used by another resource.";

  // If the resource is a View, we need to send information to the user
  // about it.
  if (type == ResourceArgs::Tag::kView || type == ResourceArgs::Tag::kView3) {
    HandleCreateView(id);
  }
}

void FakeSession::HandleGfxReleaseResource(const fuchsia::ui::gfx::ReleaseResourceCmd& cmd) {
  auto it = resources_.find(cmd.id);
  FXL_CHECK(it != resources_.end()) << "Attempting to release unknown resource ID " << cmd.id;
  resources_.erase(it);
}

void FakeSession::HandleSetEventMask(const fuchsia::ui::gfx::SetEventMaskCmd& cmd) {
  // Ensure the request is asking about a resource the client has installed.
  FXL_CHECK(resources_.find(cmd.id) != resources_.end()) << "Unknown resource ID " << cmd.id;

  // Send scaling factors client should apply when generating textures.
  //
  // Clients refuse to start rendering until they have this information.
  if (cmd.event_mask & fuchsia::ui::gfx::kMetricsEventMask) {
    fuchsia::ui::gfx::Event response;
    auto& metrics = response.metrics();
    metrics.metrics = {/*scale_x=*/1.0F, /*scale_y=*/1.0F, /*scale_z=*/1.0F};
    metrics.node_id = cmd.id;
    SendGfxEvent(std::move(response));
  }
}

void FakeSession::Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) {
  FXL_CHECK(binding_.is_bound());

  for (auto& cmd : cmds) {
    if (kTraceCommands) {
      FXL_LOG(INFO) << "Received command: " << cmd;
    }
    if (cmd.is_gfx()) {
      HandleGfxCommand(std::move(cmd.gfx()));
    }
  }
}

void FakeSession::SendGfxEvent(fuchsia::ui::gfx::Event event) {
  fuchsia::ui::scenic::Event payload;
  payload.set_gfx(std::move(event));
  SendEvent(std::move(payload));
}

void FakeSession::SendEvent(Event event) {
  if (listener_.is_bound()) {
    std::vector<Event> events;
    events.push_back(std::move(event));
    listener_->OnScenicEvent(std::move(events));
  }
}

void FakeSession::NotImplemented_(const std::string& name) {}

std::vector<const fuchsia::ui::gfx::CreateResourceCmd*> FakeSession::FindResourceByType(
    ResourceArgs::Tag type) {
  std::vector<const fuchsia::ui::gfx::CreateResourceCmd*> results;
  for (const auto& item : resources_) {
    if (item.second.resource.Which() == type) {
      results.push_back(&item.second);
    }
  }
  return results;
}

zx_status_t FakeSession::CaptureScreenshot(Screenshot* output) {
  FXL_CHECK(output != nullptr);

  // Fetch all memory objects. We assume that this corresponds to the
  // guest's framebuffer.
  std::vector<const fuchsia::ui::gfx::CreateResourceCmd*> resources =
      FindResourceByType(ResourceArgs::Tag::kMemory);
  if (resources.empty()) {
    FXL_LOG(ERROR) << "No frame buffer found.";
    return ZX_ERR_BAD_STATE;
  }
  if (resources.size() > 1) {
    FXL_LOG(ERROR) << "Multiple possible frame buffers found, which is not "
                      "supported by FakeScenic.";
    return ZX_ERR_BAD_STATE;
  }
  const fuchsia::ui::gfx::MemoryArgs& args = resources[0]->resource.memory();

  // Read from the VMO into memory.
  std::vector<std::byte> image(args.allocation_size);
  if (zx_status_t status = args.vmo.read(image.data(), 0, image.size()); status != ZX_OK) {
    return status;
  }

  // Pass data back to the user.
  output->data = std::move(image);
  output->height = kScreenHeightPixels;
  output->width = kScreenWidthPixels;
  return ZX_OK;
}

void FakeSession::set_error_handler(fit::function<void(zx_status_t)> error_handler) {
  binding_.set_error_handler(std::move(error_handler));
}

fidl::InterfaceRequestHandler<Scenic> FakeScenic::GetHandler() {
  return bindings_.GetHandler(this);
}

void FakeScenic::SendEvent(Event event) {
  if (session_.has_value()) {
    session_->SendEvent(std::move(event));
  }
}

void FakeScenic::SendKeyEvent(KeyboardEventHidUsage usage,
                              fuchsia::ui::input::KeyboardEventPhase phase) {
  Event event;
  auto& keyboard_event = event.input().keyboard();
  keyboard_event.device_id = 0;
  keyboard_event.phase = phase;
  keyboard_event.hid_usage = static_cast<uint16_t>(usage);
  keyboard_event.code_point = 0;
  SendEvent(std::move(event));
}

void FakeScenic::SendKeyPress(KeyboardEventHidUsage usage) {
  SendKeyEvent(usage, KeyboardEventPhase::PRESSED);
  SendKeyEvent(usage, KeyboardEventPhase::RELEASED);
}

zx_status_t FakeScenic::CaptureScreenshot(Screenshot* output) {
  if (!session_.has_value()) {
    return ZX_ERR_BAD_STATE;
  }

  return session_->CaptureScreenshot(output);
}

void FakeScenic::CreateSession(fidl::InterfaceRequest<Session> session_request,
                               fidl::InterfaceHandle<SessionListener> listener) {
  // Ensure we don't already have a session open.
  if (session_.has_value()) {
    FXL_LOG(WARNING) << "Attempt to create a second session on FakeScenic was rejected.";
    session_request.Close(ZX_ERR_NO_RESOURCES);
    return;
  }

  // Create a new session.
  session_.emplace(std::move(session_request), std::move(listener));
  session_->set_error_handler([this](zx_status_t /*error*/) { session_.reset(); });
}

void FakeScenic::NotImplemented_(const std::string& name) {}
