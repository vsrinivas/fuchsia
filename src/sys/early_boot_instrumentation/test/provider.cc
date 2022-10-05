// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <fidl/fuchsia.boot/cpp/markers.h>
#include <fidl/fuchsia.boot/cpp/wire.h>
#include <fidl/fuchsia.debugdata/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/stdcompat/source_location.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/service.h>
#include <lib/vfs/cpp/vmo_file.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

#include <vector>

#include <fbl/unique_fd.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

// This a test component, with the sole job of providing a fake /boot and /svc to its parent, who
// later will reroute it to its child.

namespace {

struct PublisherPayload {
  static constexpr std::string_view kSink = "llvm-profile";
  static constexpr std::string_view kCustomSink = "my-custom-sink";

  bool Init(std::string_view name,
            cpp20::source_location curr = cpp20::source_location::current()) {
    if (auto res = zx::vmo::create(4096, 0, &vmo); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to init PublisherPayload vmo at " << curr.line()
                     << ". Error: " << zx_status_get_string(res);
      return false;
    }

    if (!name.empty()) {
      if (auto res = vmo.set_property(ZX_PROP_NAME, name.data(), name.size()); res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to init PublisherPayload vmo name at " << curr.line()
                       << ". Error: " << zx_status_get_string(res);
        return false;
      }
    }

    if (auto res = zx::eventpair::create(0, &token_1, &token_2); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to init PublisherPayload tokens at " << curr.line()
                     << ". Error: " << zx_status_get_string(res);
      return false;
    }

    return true;
  }

  zx::vmo vmo;
  zx::eventpair token_1, token_2;
};

struct ChannelPair {
  bool Init(cpp20::source_location curr = cpp20::source_location::current()) {
    if (auto res = zx::channel::create(0, &read, &write); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to init ChannelPair " << curr.line()
                     << ". Error: " << zx_status_get_string(res);
      return false;
    }
    return true;
  }

  zx::channel read, write;
};

struct Channels {
  bool Init() {
    // Using empty inline comments to force linebreak, so the source line is unique for each
    // ChannelPair.
    return svc.Init() &&        //
           svc_stash.Init() &&  //
           publisher.Init();
  }

  ChannelPair svc;
  ChannelPair svc_stash;
  ChannelPair publisher;
};

class ProviderServer final : public fidl::WireServer<fuchsia_boot::SvcStashProvider> {
 public:
  ProviderServer(async_dispatcher_t* dispatcher,
                 fidl::ServerEnd<fuchsia_boot::SvcStashProvider> server_end)
      : binding_(fidl::BindServer(dispatcher, std::move(server_end), this)) {
    channels_ = std::make_unique<Channels>();
    payload_1_ = std::make_unique<PublisherPayload>();
    payload_2_ = std::make_unique<PublisherPayload>();
    payload_3_ = std::make_unique<PublisherPayload>();
    payload_4_ = std::make_unique<PublisherPayload>();
    FillStash();
  }

  void Get(GetCompleter::Sync& completer) final {
    // So the test can be repeated, each call to Get refills the stash with the expected values.
    fuchsia_boot::wire::SvcStashProviderGetResponse response;
    response.resource = std::move(stash_);
    completer.Reply(fit::ok(&response));
  }

 private:
  void FillStash() {
    if (!channels_->Init()) {
      FX_LOGS(ERROR) << "Failed to initialize channels for Publisher data.";
    } else {
      if (!payload_1_->Init("profraw") ||  // llvm-profile static
          !payload_2_->Init("profraw") ||  // llvm-profile dynamic
          !payload_3_->Init("custom") ||   // custom static
          !payload_4_->Init("")) {         // custom dynamic
        FX_LOGS(ERROR) << "Failed to initialize payloads for Publisher data.";
      }

      // payload_1 is static.
      payload_1_->token_1.reset();
      payload_3_->token_1.reset();

      // Add some unique content to the vmos.
      if (auto res = payload_1_->vmo.write("1234", 0, 4); res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to initialize payload_1_ content for Publisher data.";
      }

      if (auto res = payload_2_->vmo.write("567890123", 0, 9); res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to initialize payload_2_ content for Publisher data.";
      }

      // Add some unique content to the vmos.
      if (auto res = payload_3_->vmo.write("789", 0, 3); res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to initialize payload_3_ content for Publisher data.";
      }

      if (auto res = payload_4_->vmo.write("43218765", 0, 8); res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to initialize payload_4_ content for Publisher data.";
      }
    }

    // Publish data to publisher_client.
    if (auto res =
            fdio_service_connect_at(channels_->svc.write.get(),
                                    fidl::DiscoverableProtocolName<fuchsia_debugdata::Publisher>,
                                    channels_->publisher.read.release());
        res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to pipeline open request to svc.";
    } else {
      fidl::WireSyncClient<fuchsia_debugdata::Publisher> publisher;
      fidl::ClientEnd<fuchsia_debugdata::Publisher> publisher_client_end(
          std::move(channels_->publisher.write));
      publisher.Bind(std::move(publisher_client_end));

      if (auto res = publisher
                         ->Publish(fidl::StringView::FromExternal(PublisherPayload::kSink),
                                   std::move(payload_1_->vmo), std::move(payload_1_->token_2))
                         .status();
          res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to publish payload_1_. Error: " << zx_status_get_string(res);
      }

      if (auto res = publisher
                         ->Publish(fidl::StringView::FromExternal(PublisherPayload::kSink),
                                   std::move(payload_2_->vmo), std::move(payload_2_->token_2))
                         .status();
          res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to publish payload_2_. Error: " << zx_status_get_string(res);
      }

      if (auto res = publisher
                         ->Publish(fidl::StringView::FromExternal(PublisherPayload::kCustomSink),
                                   std::move(payload_3_->vmo), std::move(payload_3_->token_2))
                         .status();
          res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to publish payload_3_. Error: " << zx_status_get_string(res);
      }

      if (auto res = publisher
                         ->Publish(fidl::StringView::FromExternal(PublisherPayload::kCustomSink),
                                   std::move(payload_4_->vmo), std::move(payload_4_->token_2))
                         .status();
          res != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to publish payload_4_. Error: " << zx_status_get_string(res);
      }
    }

    fidl::WireSyncClient<fuchsia_boot::SvcStash> svc_stash;
    fidl::ClientEnd<fuchsia_boot::SvcStash> svc_stash_client_end(
        std::move(channels_->svc_stash.write));
    svc_stash.Bind(std::move(svc_stash_client_end));
    fidl::ServerEnd<fuchsia_io::Directory> svc_server_end(std::move(channels_->svc.read));
    if (auto res = svc_stash->Store(std::move(svc_server_end)).status(); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to store svc in SvcStash. Error: " << zx_status_get_string(res);
    }
    stash_ = fidl::ServerEnd<fuchsia_boot::SvcStash>(std::move(channels_->svc_stash.read));
  }

  fidl::ServerBindingRef<fuchsia_boot::SvcStashProvider> binding_;
  fidl::ServerEnd<fuchsia_boot::SvcStash> stash_;

  std::unique_ptr<Channels> channels_;
  std::unique_ptr<PublisherPayload> payload_1_, payload_2_, payload_3_, payload_4_;
};

}  // namespace

int main(int argc, char** argv) {
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line,
                                     {"early-boot-instrumentation", "capability-provider"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  // Bind the Provider server.
  std::vector<std::unique_ptr<ProviderServer>> connections;
  auto provider_svc = std::make_unique<vfs::Service>(
      [&connections](zx::channel request, async_dispatcher_t* dispatcher) {
        fidl::ServerEnd<fuchsia_boot::SvcStashProvider> server_end(std::move(request));
        auto connection = std::make_unique<ProviderServer>(dispatcher, std::move(server_end));
        connections.push_back(std::move(connection));
      });
  if (context->outgoing()->AddPublicService(
          std::move(provider_svc),
          fidl::DiscoverableProtocolName<fuchsia_boot::SvcStashProvider>) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to add provider servicer.";
  }

  // Add prof_data dir.
  auto* boot = context->outgoing()->GetOrCreateDirectory("boot");
  auto kernel = std::make_unique<vfs::PseudoDir>();
  auto data = std::make_unique<vfs::PseudoDir>();
  auto phys = std::make_unique<vfs::PseudoDir>();

  // Fake Kernel vmo.
  zx::vmo kernel_vmo;
  if (auto res = zx::vmo::create(4096, 0, &kernel_vmo); res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create Kernel VMO. Error: " << zx_status_get_string(res);
  } else {
    if (auto res = kernel_vmo.write("kernel", 0, 7); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to write Kernel VMO contents. Error: " << zx_status_get_string(res);
    }
    auto kernel_file = std::make_unique<vfs::VmoFile>(std::move(kernel_vmo), 4096);
    data->AddEntry("zircon.elf.profraw", std::move(kernel_file));
  }

  // Fake Physboot VMO.
  zx::vmo phys_vmo;
  if (auto res = zx::vmo::create(4096, 0, &phys_vmo); res != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create Physboot VMO. Error: " << zx_status_get_string(res);
  } else {
    if (auto res = phys_vmo.write("physboot", 0, 9); res != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to write Kernel VMO contents. Error: " << zx_status_get_string(res);
    }
    auto physboot_file = std::make_unique<vfs::VmoFile>(std::move(phys_vmo), 4096);
    phys->AddEntry("physboot.profraw", std::move(physboot_file));
  }

  // outgoing/boot/kernel/data/phys
  data->AddEntry("phys", std::move(phys));
  // outgoing/boot/kernel/data
  kernel->AddEntry("data", std::move(data));
  // outgoing/boot/kernel
  boot->AddEntry("kernel", std::move(kernel));

  loop.Run();
  return 0;
}
