// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "WaterfallViewController.h"

#include <memory>

#include "escher/renderer.h"
#include "examples/waterfall/scenes/app_test_scene.h"
#include "examples/waterfall/scenes/material_stage.h"
#include "examples/waterfall/scenes/shadow_test_scene.h"

constexpr bool kDrawShadowTestScene = false;

@interface WaterfallViewController () {
  escher::Stage stage_;
  std::unique_ptr<escher::Renderer> renderer_;
  glm::vec2 focus_;
  AppTestScene app_test_scene_;
  ShadowTestScene shadow_test_scene_;
}

@property(strong, nonatomic) EAGLContext* context;

@end

@implementation WaterfallViewController

- (void)viewDidLoad {
  [super viewDidLoad];

  self.context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
  self.preferredFramesPerSecond = 60;

  if (!self.context) {
    NSLog(@"Failed to create ES context");
  }

  GLKView* view = (GLKView*)self.view;
  view.context = self.context;
  view.drawableDepthFormat = GLKViewDrawableDepthFormat24;

  [EAGLContext setCurrentContext:self.context];

  app_test_scene_.InitGL();
  InitStageForMaterial(&stage_);
  renderer_.reset(new escher::Renderer());

  if (!renderer_->Init()) {
    NSLog(@"Failed to initialize renderer");
  }

  CGSize size = self.view.bounds.size;
  focus_ = glm::vec2(size.width / 2.0f, size.height / 2.0f);
  [self update];
}

- (void)dealloc {
  [EAGLContext setCurrentContext:self.context];
  renderer_.reset();
  [EAGLContext setCurrentContext:nil];
}

- (BOOL)prefersStatusBarHidden {
  return YES;
}

- (void)update {
  CGFloat contentScaleFactor = self.view.contentScaleFactor;
  CGSize size = self.view.bounds.size;
  stage_.Resize(escher::SizeI(size.width * contentScaleFactor,
                              size.height * contentScaleFactor),
                contentScaleFactor);
}

- (void)glkView:(GLKView*)view drawInRect:(CGRect)rect {
  // TODO(abarth): There must be a better way to initialize this information.
  if (!renderer_->front_frame_buffer_id()) {
    GLint fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
    renderer_->set_front_frame_buffer_id(fbo);
  }

  if (kDrawShadowTestScene)
    renderer_->Render(stage_, shadow_test_scene_.GetModel(stage_.viewing_volume()));
  else
    renderer_->Render(stage_, app_test_scene_.GetModel(stage_.viewing_volume(), focus_));
}

- (void)touchesMoved:(NSSet*)touches withEvent:(UIEvent*)event {
    for (UITouch* touch in touches) {
        CGPoint windowCoordinates = [touch locationInView:nil];
        focus_ = glm::vec2(windowCoordinates.x, windowCoordinates.y);
    }
    [self.view setNeedsDisplay];
}

@end
