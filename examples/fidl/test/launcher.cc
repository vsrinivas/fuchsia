// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/service_directory.h>

void LaunchServer(std::string server_url, const sys::ServiceDirectory& svc,
                  fidl::InterfaceRequest<fuchsia::io::Directory> directory_request,
                  fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl_request) {
  fuchsia::sys::LauncherPtr launcher;
  svc.Connect(launcher.NewRequest());

  fuchsia::sys::LaunchInfo server_info{
      .url = std::move(server_url),
      .directory_request = directory_request.TakeChannel(),
  };
  launcher->CreateComponent(std::move(server_info), std::move(ctrl_request));
}

void CreateNestedEnv(std::vector<std::string> protocol_names, const sys::ServiceDirectory& svc,
                     fidl::InterfaceHandle<fuchsia::io::Directory> directory,
                     fidl::InterfaceRequest<fuchsia::sys::EnvironmentController> env_ctrl_request,
                     fidl::InterfaceRequest<fuchsia::sys::Launcher> launcher_request) {
  fuchsia::sys::EnvironmentPtr env;
  svc.Connect(env.NewRequest());
  fuchsia::sys::EnvironmentPtr nested_env;
  std::unique_ptr<fuchsia::sys::ServiceList> services(new fuchsia::sys::ServiceList);
  services->names = std::move(protocol_names);
  services->host_directory = directory.TakeChannel();
  fuchsia::sys::EnvironmentOptions options;
  options.inherit_parent_services = true;
  env->CreateNestedEnvironment(nested_env.NewRequest(), std::move(env_ctrl_request), "echo",
                               std::move(services), std::move(options));
  nested_env->GetLauncher(std::move(launcher_request));
}

void LaunchClient(std::string client_url, fuchsia::sys::LauncherPtr launcher,
                  fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl_request) {
  fuchsia::sys::LaunchInfo client_info{.url = std::move(client_url)};
  launcher->CreateComponent(std::move(client_info), std::move(ctrl_request));
}

int64_t LaunchComponents(std::string client_url, std::string server_url,
                         std::vector<std::string> capability_names) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto svc = sys::ServiceDirectory::CreateFromNamespace();
  fidl::InterfaceHandle<fuchsia::io::Directory> directory;
  fuchsia::sys::ComponentControllerPtr server_controller;
  LaunchServer(std::move(server_url), *svc, directory.NewRequest(), server_controller.NewRequest());

  // create nested environment with the server's capabilities
  fuchsia::sys::EnvironmentControllerPtr nested_env_ctrl;
  fuchsia::sys::LauncherPtr client_launcher;
  CreateNestedEnv(std::move(capability_names), *svc, std::move(directory),
                  nested_env_ctrl.NewRequest(), client_launcher.NewRequest());

  fuchsia::sys::ComponentControllerPtr client_controller;
  LaunchClient(std::move(client_url), std::move(client_launcher), client_controller.NewRequest());

  // terminate once client terminates
  int64_t client_status = -1;
  client_controller.events().OnTerminated =
      [&loop, &client_status](uint64_t code, fuchsia::sys::TerminationReason reason) {
        client_status = code;
        printf("client exit code: %lu, reason: %d\n", code, reason);
        loop.Quit();
      };

  loop.Run();
  return client_status;
}
