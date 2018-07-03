// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_MODULE_RESOLVER_TYPE_INFERENCE_H_
#define PERIDOT_BIN_MODULE_RESOLVER_TYPE_INFERENCE_H_

#include <string>
#include <vector>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/operation.h>

namespace modular {

class ParameterTypeInferenceHelper {
 public:
  ParameterTypeInferenceHelper(
      fuchsia::modular::EntityResolverPtr entity_resolver);
  ~ParameterTypeInferenceHelper();

  // Returns a list of types represented in |parameter_constraint|. Chooses the
  // correct process for type extraction based on the type of Parameter.
  void GetParameterTypes(
      const fuchsia::modular::ResolverParameterConstraint& parameter_constraint,
      const std::function<void(std::vector<std::string>)>& result_callback);

 private:
  class GetParameterTypesCall;

  fuchsia::modular::EntityResolverPtr entity_resolver_;
  OperationCollection operation_collection_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ParameterTypeInferenceHelper);
};

}  // namespace modular

#endif  // PERIDOT_BIN_MODULE_RESOLVER_TYPE_INFERENCE_H_
