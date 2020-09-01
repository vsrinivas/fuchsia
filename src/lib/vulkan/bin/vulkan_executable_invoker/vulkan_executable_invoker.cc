// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef EXECUTABLE
#error FATAL: EXECUTABLE not defined!
#endif

#ifndef VK_LAYER_PATH
#error Fatal: VK_LAYER_PATH not defined!
#endif

#ifndef VK_LIB_PATH
#error Fatal: VK_LIB_PATH not defined!
#endif

namespace {

std::filesystem::path GetSelfDirectory() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(&result[0], &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
#error Write this for your platform.
#endif

  char fullpath[4096];
  return std::filesystem::path(realpath(result.c_str(), fullpath)).remove_filename();
}

// Return true iff |*env| is an envp entry for |varname|.
bool EnvironmentHasVarname(const char* env, const char* varname) {
  std::string prefix = std::string(varname) + "=";
  return !memcmp(prefix.c_str(), env, prefix.size());
};

}  // namespace

int main(int argc, char** argv, char** envp) {
  auto self_dir = GetSelfDirectory();
  auto vk_lib_path = self_dir / VK_LIB_PATH;
  auto vk_layer_path = self_dir / VK_LAYER_PATH;
#ifdef VK_ICD_PATH
  auto vk_icd_path = self_dir / VK_ICD_PATH;
#endif
  auto executable_path = self_dir / EXECUTABLE;

  // If LD_LIBRARY_PATH was previously defined, we append prebuilt library path
  // to it so that predefined library path will have priority in library lookup.
  // Otherwise, we just set it to the prebuilt library and avoid adding a ":"
  // prefix to avoid looking up libraries in the current working directory.
  const char* ld_library_path_cstr = getenv("LD_LIBRARY_PATH");
  std::string ld_library_path = ld_library_path_cstr
                                    ? std::string(ld_library_path_cstr) + ":" + vk_lib_path.string()
                                    : vk_lib_path.string();

  // Set up envp.
  std::vector<char*> environment_pointers;
  std::vector<std::string> environments = {
      std::string("LD_LIBRARY_PATH=") + ld_library_path,
      std::string("VK_LAYER_PATH=") + vk_layer_path.string(),
#ifdef VK_ICD_PATH
      std::string("VK_ICD_FILENAMES=") + vk_icd_path.string(),
#endif
  };
  for (const std::string& env : environments) {
    environment_pointers.push_back(const_cast<char*>(env.c_str()));
  }

  // And environment variables from host environment.
  for (char** env = envp; *env; env++) {
    bool filter_out = EnvironmentHasVarname(*env, "LD_LIBRARY_PATH") ||
                      EnvironmentHasVarname(*env, "VK_LAYER_PATH");
#ifdef VK_ICD_PATH
    filter_out = filter_out || EnvironmentHasVarname(*env, "VK_ICD_FILENAMES");
#endif
    if (!filter_out) {
      environment_pointers.push_back(*env);
    }
  }
  environment_pointers.push_back(nullptr);

  // Set up argv.
  std::vector<char*> arguments;
  arguments.push_back(const_cast<char*>(executable_path.c_str()));
  for (int i = 1; i < argc; i++) {
    arguments.push_back(argv[i]);
  }

  execve(arguments[0], arguments.data(), environment_pointers.data());

  // This should be reached only if execve() fails.
  perror("execve() fails");
  exit(EXIT_FAILURE);
}
