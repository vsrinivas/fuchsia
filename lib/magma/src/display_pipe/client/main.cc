// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/app/cpp/connect.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "zircon/status.h"
#include "garnet/lib/magma/src/display_pipe/client/buffer.h"
#include "garnet/lib/magma/src/display_pipe/services/display_provider.fidl.h"

display_pipe::DisplayProviderPtr display;
scenic::ImagePipePtr image_pipe;
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

class BufferHandler : public fsl::MessageLoopHandler {
 public:
  BufferHandler(Buffer *buffer, uint32_t index) :
      buffer_(buffer), index_(index) {
    handler_key_ =
        fsl::MessageLoop::GetCurrent()->AddHandler(this,
                                                   buffer->release_fence().get(),
                                                   ZX_EVENT_SIGNALED);
  }

  ~BufferHandler() override = default;

  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override {
      buffer_->Reset();

      auto acq = fidl::Array<zx::event>::New(1);
      auto rel = fidl::Array<zx::event>::New(1);
      buffer_->dupAcquireFence(&acq.front());
      buffer_->dupReleaseFence(&rel.front());

      image_pipe->PresentImage(index_, 0, std::move(acq), std::move(rel),
                               [](scenic::PresentationInfoPtr info) {});

      uint8_t r, g, b;
      hsv_color(hsv_index, &r, &g, &b);
      hsv_index = hsv_inc(hsv_index, 3);
      buffer_->Fill(r, g, b);

      buffer_->Signal();
  }

  void OnHandleError(zx_handle_t handle, zx_status_t error) override {
      FXL_LOG(ERROR) << "BufferHandler received an error ("
          << zx_status_get_string(error) << ").  Exiting.";
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
  };

 private:
  Buffer *buffer_;
  uint32_t index_;
  fsl::MessageLoop::HandlerKey handler_key_;

};

Buffer *buffers[2];
BufferHandler *handlers[2];

void allocate_buffer(uint32_t index, uint32_t width, uint32_t height) {
    Buffer *buffer = Buffer::NewBuffer(width, height);
    buffers[index] = buffer;

    auto info = scenic::ImageInfo::New();
    info->width = width;
    info->height = height;
    info->stride = width * 4;
    info->pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
    info->color_space = scenic::ImageInfo::ColorSpace::SRGB;

    zx::vmo vmo;
    buffer->dupVmo(&vmo);
    image_pipe->AddImage(index, std::move(info), std::move(vmo),
                         scenic::MemoryType::HOST_MEMORY, 0);

    handlers[index] = new BufferHandler(buffer, index);
}

int main(int argc, char* argv[]) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  fsl::MessageLoop loop;

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
