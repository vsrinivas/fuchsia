// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This component launches the codelab examples. The example contains directories for each part of
// the codelab, and this component accepts a command line argument to choose which part to launch.
//
// In addition to launching the codelab, this component also launches the fizzbuzz component that
// the codelab depends on.

#include <fuchsia/examples/inspect/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdio.h>

#include <chrono>
#include <iostream>
#include <thread>

constexpr char fizzbuzz_url[] =
    "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab#meta/inspect_cpp_codelab_fizzbuzz.cmx";

std::string GetComponentUrl(const std::string& argument) {
  return "fuchsia-pkg://fuchsia.com/inspect_cpp_codelab#meta/inspect_cpp_codelab_part_" + argument +
         ".cmx";
}

void Usage(char* name) {
  printf(
      "Usage: %s <option> <string> [string...]\n  option: The server to run. For example \"1\" for "
      "part_1\n  string: Strings provided on the command line to reverse\n",
      name ? name : "");
  exit(1);
}

int main(int argc, char** argv) {
  // Parse the input argument to create a component URL.
  // This component launches the server and connects to it, but first we need to know which part of
  // the codelab to start.
  std::string reverser_component_url;
  if (argc >= 3) {
    reverser_component_url = GetComponentUrl(argv[1]);
  }

  // If no url is specified, print the usage information and exit.
  if (reverser_component_url == "") {
    Usage(argc >= 1 ? argv[0] : nullptr);
  }

  auto incoming_services = sys::ServiceDirectory::CreateFromNamespace();

  fuchsia::sys::EnvironmentSyncPtr env;
  incoming_services->Connect(env.NewRequest());

  fuchsia::sys::EnvironmentSyncPtr child_env;
  fuchsia::sys::EnvironmentControllerSyncPtr child_env_controller;
  fuchsia::sys::LauncherSyncPtr launcher;

  zx::channel fizzbuzz_directory_client, fizzbuzz_directory_server;
  {
    fuchsia::io::DirectorySyncPtr req;
    fizzbuzz_directory_server = req.NewRequest().TakeChannel();
    fizzbuzz_directory_client = req.Unbind().TakeChannel();
  }

  // We need to include the FizzBuzz service in the environment we are creating for the codelab.
  // This code adds additional services to the nested environment so it knows to look for the
  // service in the out/svc directory of the newly created component.
  auto env_service_list = std::make_unique<fuchsia::sys::ServiceList>();
  env_service_list->names.push_back(fuchsia::examples::inspect::FizzBuzz::Name_);
  env_service_list->host_directory = std::move(fizzbuzz_directory_client);

  // Create an environment called "codelab" to contain the new components.
  if (ZX_OK != env->CreateNestedEnvironment(child_env.NewRequest(),
                                            child_env_controller.NewRequest(), "codelab",
                                            std::move(env_service_list) /* additional_services */,
                                            {.inherit_parent_services = true} /* options */)) {
    printf("Failed to create nested environment\n");
    exit(1);
  }

  // Get the launcher needed to create components in the nested environment.
  if (ZX_OK != child_env->GetLauncher(launcher.NewRequest())) {
    printf("Failed to get launcher\n");
    exit(1);
  }

  // Launch the FizzBuzz service component by URL.
  {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = fizzbuzz_url;
    launch_info.directory_request = std::move(fizzbuzz_directory_server);
    if (ZX_OK != launcher->CreateComponent(std::move(launch_info), nullptr /* controller */)) {
      printf("Failed to launch component %s\n", fizzbuzz_url);
      exit(1);
    }
  }

  // Launch the component by URL.
  fuchsia::io::DirectorySyncPtr directory_request;
  {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = reverser_component_url;
    launch_info.directory_request = directory_request.NewRequest().TakeChannel();
    if (ZX_OK != launcher->CreateComponent(std::move(launch_info), nullptr /* controller */)) {
      printf("Failed to launch component %s\n", reverser_component_url.c_str());
      exit(1);
    }
  }

  // Create a service directory to connect to services exposed by the created component.
  sys::ServiceDirectory services(directory_request.Unbind());

  // Connect to the reverser service provided by the launched component.
  fuchsia::examples::inspect::ReverserSyncPtr reverser;
  services.Connect(reverser.NewRequest());

  // [START reverse_loop]
  // Repeatedly send strings to be reversed to the other component.
  for (int i = 2; i < argc; i++) {
    printf("Input: %s\n", argv[i]);

    std::string output;
    if (ZX_OK != reverser->Reverse(argv[i], &output)) {
      printf("Error: Failed to reverse string.\nPerhaps %s was not found?\n",
             reverser_component_url.c_str());
      exit(1);
    }

    printf("Output: %s\n", output.c_str());
    fflush(stdout);
  }
  // [END reverse_loop]

  printf("Done. Press Ctrl+C to exit\n");
  fflush(stdout);
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return 0;
}
