// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/compositor/glue/gl/gl_context.h"

#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/interfaces/application/application_connector.mojom.h"
#include "mojo/services/gpu/interfaces/gpu.mojom.h"

namespace mojo {

GLContext::GLContext(InterfaceHandle<CommandBuffer> command_buffer)
    : mgl_context_(
          MGLCreateContext(MGL_API_VERSION_GLES2,
                           command_buffer.PassHandle().release().value(),
                           MGL_NO_CONTEXT,
                           &ContextLostThunk,
                           this,
                           Environment::GetDefaultAsyncWaiter())) {
  FTL_DCHECK(mgl_context_ != MGL_NO_CONTEXT);
}

GLContext::~GLContext() {
  MGLDestroyContext(mgl_context_);
}

ftl::RefPtr<GLContext> GLContext::CreateOffscreen(
    ApplicationConnector* connector) {
  ServiceProviderPtr native_viewport;
  connector->ConnectToApplication("mojo:native_viewport_service",
                                  GetProxy(&native_viewport));
  GpuPtr gpu_service;
  ConnectToService(native_viewport.get(), GetProxy(&gpu_service));
  InterfaceHandle<CommandBuffer> command_buffer;
  gpu_service->CreateOffscreenGLES2Context(GetProxy(&command_buffer));
  return ftl::MakeRefCounted<GLContext>(command_buffer.Pass());
}

ftl::RefPtr<GLContext> GLContext::CreateFromCommandBuffer(
    InterfaceHandle<CommandBuffer> command_buffer) {
  return ftl::MakeRefCounted<GLContext>(command_buffer.Pass());
}

bool GLContext::IsCurrent() const {
  return mgl_context_ == MGLGetCurrentContext();
}

void GLContext::AddObserver(Observer* observer) {
  FTL_DCHECK(observer);
  observers_.push_back(observer);
}

void GLContext::RemoveObserver(Observer* observer) {
  FTL_DCHECK(observer);
  auto it = std::find(observers_.begin(), observers_.end(), observer);
  if (it != observers_.end())
    observers_.erase(it);
}

void GLContext::ContextLostThunk(void* self) {
  static_cast<GLContext*>(self)->OnContextLost();
}

void GLContext::OnContextLost() {
  FTL_DCHECK(!lost_);

  lost_ = true;
  while (!observers_.empty()) {
    Observer* observer = observers_.front();
    observers_.pop_front();
    observer->OnContextLost();
  }
}

GLContext::Scope::Scope(const ftl::RefPtr<GLContext>& gl_context)
    : gl_context_(gl_context), prior_mgl_context_(MGLGetCurrentContext()) {
  FTL_DCHECK(gl_context_);
  FTL_CHECK(!gl_context_->is_lost());  // common bug, check it in release builds

  MGLMakeCurrent(gl_context_->mgl_context_);
}

GLContext::Scope::~Scope() {
  FTL_DCHECK(gl_context_->IsCurrent());

  MGLMakeCurrent(prior_mgl_context_);
}

GLContext::Observer::~Observer() {}

}  // namespace mojo
