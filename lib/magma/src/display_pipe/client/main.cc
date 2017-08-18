// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "application/lib/app/connect.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings_command_line.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"
#include "magenta/status.h"
#include "magma/src/display_pipe/client/buffer.h"
#include "magma/src/display_pipe/services/display_provider.fidl.h"

display_pipe::DisplayProviderPtr display;
mozart2::ImagePipePtr image_pipe;
uint64_t hsv_index;

// HSV code adopted from:
//   https://github.com/konkers/lk-firmware/blob/master/app/robot/hsv.h
static void hsv_color(uint32_t index, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t pos = index & 0xff;
    uint8_t neg = 0xff - (index & 0xff);
    uint8_t phase = (index >> 8) & 0x7;

    switch (phase) {
        case 0:
            *r = pos;
            *g = 0x00;
            *b = 0xff;
            break;

        case 1:
            *r = 0xff;
            *g = 0x00;
            *b = neg;
            break;

        case 2:
            *r = 0xff;
            *g = pos;
            *b = 0x00;
            break;

        case 3:
            *r = neg;
            *g = 0xff;
            *b = 0x00;
            break;

        case 4:
            *r = 0x00;
            *g = 0xff;
            *b = pos;
            break;

        default:
        case 5:
            *r = 0x00;
            *g = neg;
            *b = 0xff;
            break;
    }
}

static uint32_t hsv_inc(uint32_t index, int16_t inc) {
    int32_t signed_index = index + inc;
    while (signed_index >= 0x600)
        signed_index -= 0x600;
    while (signed_index < 0)
        signed_index += 0x600;
    return signed_index;
}

class BufferHandler : public mtl::MessageLoopHandler {
 public:
  BufferHandler(Buffer *buffer, uint32_t index) :
      buffer_(buffer), index_(index) {
    handler_key_ =
        mtl::MessageLoop::GetCurrent()->AddHandler(this,
                                                   buffer->release_fence().get(),
                                                   MX_EVENT_SIGNALED);
  }

  ~BufferHandler() override = default;

  void OnHandleReady(mx_handle_t handle,
                     mx_signals_t pending,
                     uint64_t count) override {
      buffer_->Reset();
      mx::event acq, rel;
      buffer_->dupAcquireFence(&acq);
      buffer_->dupReleaseFence(&rel);

      image_pipe->PresentImage(index_, 0, std::move(acq), std::move(rel),
                               [](mozart2::PresentationInfoPtr info) {});

      uint8_t r, g, b;
      hsv_color(hsv_index, &r, &g, &b);
      hsv_index = hsv_inc(hsv_index, 3);
      buffer_->Fill(r, g, b);

      buffer_->Signal();
  }

  void OnHandleError(mx_handle_t handle, mx_status_t error) override {
      FTL_LOG(ERROR) << "BufferHandler received an error ("
          << mx_status_get_string(error) << ").  Exiting.";
      mtl::MessageLoop::GetCurrent()->PostQuitTask();
  };

 private:
  Buffer *buffer_;
  uint32_t index_;
  mtl::MessageLoop::HandlerKey handler_key_;

};

Buffer *buffers[2];
BufferHandler *handlers[2];

void allocate_buffer(uint32_t index, uint32_t width, uint32_t height) {
    Buffer *buffer = Buffer::NewBuffer(width, height);
    buffers[index] = buffer;

    auto info = mozart2::ImageInfo::New();
    info->width = width;
    info->height = height;
    info->stride = width * 4;
    info->pixel_format = mozart2::ImageInfo::PixelFormat::BGRA_8;
    info->color_space = mozart2::ImageInfo::ColorSpace::SRGB;

    mx::vmo vmo;
    buffer->dupVmo(&vmo);
    image_pipe->AddImage(index, std::move(info), std::move(vmo),
                         mozart2::MemoryType::HOST_MEMORY, 0);

    handlers[index] = new BufferHandler(buffer, index);
}

int main(int argc, char* argv[]) {
  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  mtl::MessageLoop loop;

  auto application_context_ = app::ApplicationContext::CreateFromStartupInfo();
  app::ServiceProviderPtr services;
  display =
      application_context_->ConnectToEnvironmentService<display_pipe::DisplayProvider>();

  display->GetInfo([](display_pipe::DisplayInfoPtr info) {
      printf("%d x %d\n", info->width, info->height);
      display->BindPipe(image_pipe.NewRequest());
      allocate_buffer(0, info->width, info->height);
      allocate_buffer(1, info->width, info->height);
  });

  loop.Run();

  // In lieu of a clean shutdown, we signal all our buffer to ensure we don't
  // hang the display.
  buffers[0]->Signal();
  buffers[1]->Signal();

  return 0;
}
