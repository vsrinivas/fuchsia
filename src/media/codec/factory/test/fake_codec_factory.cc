// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <fuchsia/media/cpp/fidl_test_base.h>
#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

class StreamProcessorImpl final : public fuchsia::media::testing::StreamProcessor_TestBase {
  void NotImplemented_(const std::string& name) override {
    fprintf(stderr, "Streamprocessor doing notimplemented with %s\n", name.c_str());
  }
};

class CodecFactoryImpl final : public fuchsia::mediacodec::CodecFactory {
 public:
  void Bind(std::unique_ptr<CodecFactoryImpl> factory,
            fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request) {
    binding_.Bind(std::move(request));
    binding_.set_error_handler([factory = std::move(factory)](zx_status_t status) {});
    fuchsia::mediacodec::CodecDescription description;
    description.codec_type = fuchsia::mediacodec::CodecType::DECODER;
    description.mime_type = "video/h264";
    std::vector<fuchsia::mediacodec::CodecDescription> descriptions{description};
    binding_.events().OnCodecList(std::move(descriptions));
  }

  void CreateDecoder(fuchsia::mediacodec::CreateDecoder_Params params,
                     fidl::InterfaceRequest<fuchsia::media::StreamProcessor> decoder) override {
    StreamProcessorImpl impl;

    fidl::Binding<fuchsia::media::StreamProcessor> processor(&impl);
    processor.Bind(std::move(decoder));
    processor.events().OnInputConstraints(fuchsia::media::StreamBufferConstraints());
  }

  void CreateEncoder(
      fuchsia::mediacodec::CreateEncoder_Params encoder_params,
      fidl::InterfaceRequest<fuchsia::media::StreamProcessor> encoder_request) override {}

  void AttachLifetimeTracking(zx::eventpair codec_end) override {}

 private:
  fidl::Binding<fuchsia::mediacodec::CodecFactory> binding_{this};
};

int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Validate that /dev/class/gpu is accessible.
  fuchsia::gpu::magma::DeviceSyncPtr device;
  fdio_service_connect("/dev/class/gpu/000", device.NewRequest().TakeChannel().release());
  std::vector<fuchsia::gpu::magma::IcdInfo> list;
  zx_status_t status = device->GetIcdList(&list);
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to call GetIcdList\n");
    return -1;
  }
  if (list.size() != 1) {
    fprintf(stderr, "Incorrect ICD list size %lu\n", list.size());
    return -1;
  }

  context->outgoing()->AddPublicService(
      fidl::InterfaceRequestHandler<fuchsia::mediacodec::CodecFactory>(
          [](fidl::InterfaceRequest<fuchsia::mediacodec::CodecFactory> request) {
            auto factory = std::make_unique<CodecFactoryImpl>();
            factory->Bind(std::move(factory), std::move(request));
          }));
  loop.Run();
  return 0;
}
