// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <malloc.h>
#include <string.h>

#define CL_USE_DEPRECATED_OPENCL_1_0_APIS
#define CL_USE_DEPRECATED_OPENCL_1_1_APIS

#include "CL/cl.h"
#include "CL/cl_ext.h"
#include "CL/cl_icd.h"

struct _cl_platform_id {
  const cl_icd_dispatch *dispatch;
};

namespace {

cl_int CL_API_CALL clGetPlatformInfoKHR(cl_platform_id platform, cl_platform_info param_name,
                                        size_t param_value_size, void *param_value,
                                        size_t *param_value_size_ret) CL_API_SUFFIX__VERSION_1_0 {
  const char *returnString = NULL;
  size_t returnStringLength = 0;

  if (param_value_size == 0 && param_value != NULL) {
    return CL_INVALID_VALUE;
  }

  switch (param_name) {
    case CL_PLATFORM_PROFILE:
      returnString = "Fake Profile";
      break;
    case CL_PLATFORM_VERSION:
      returnString = "OpenCL 1.2";
      break;
    case CL_PLATFORM_NAME:
      returnString = "ICD_LOADER_TEST_OPENCL_STUB";
      break;
    case CL_PLATFORM_VENDOR:
      returnString = "Fake Vendor";
      break;
    case CL_PLATFORM_EXTENSIONS:
      returnString = "cl_khr_icd";
      break;
    case CL_PLATFORM_ICD_SUFFIX_KHR:
      returnString = "fake";
      break;
    default:
      return CL_INVALID_VALUE;
  }

  // make sure the buffer passed in is big enough for the result
  returnStringLength = strlen(returnString) + 1;
  if (param_value_size && param_value_size < returnStringLength) {
    return CL_INVALID_VALUE;
  }

  // pass the data back to the user
  if (param_value) {
    memcpy(param_value, returnString, returnStringLength);
  }
  if (param_value_size_ret) {
    *param_value_size_ret = returnStringLength;
  }

  return CL_SUCCESS;
}

constexpr cl_icd_dispatch dispatchTable = {
    .clGetPlatformInfo = &clGetPlatformInfoKHR,
};

_cl_platform_id platform = {.dispatch = &dispatchTable};

cl_int CL_API_CALL clIcdGetPlatformIDsKHR(cl_uint num_entries, cl_platform_id *platforms,
                                          cl_uint *num_platforms) {
  if (num_platforms) {
    *num_platforms = 1;
  }

  if (platforms) {
    platforms[0] = &platform;
  } else {
    if (num_entries > 0) {
      return CL_INVALID_VALUE;
    }
  }

  return CL_SUCCESS;
}

extern "C" __attribute__((visibility("default"))) void *CL_API_CALL
clGetExtensionFunctionAddress(const char *name) {
  if (strcmp("clIcdGetPlatformIDsKHR", name) == 0) {
    return reinterpret_cast<void *>(&clIcdGetPlatformIDsKHR);
  }

  return NULL;
}

}  // namespace
