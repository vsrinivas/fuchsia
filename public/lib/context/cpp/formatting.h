// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_CONTEXT_CPP_FORMATTING_H_
#define LIB_CONTEXT_CPP_FORMATTING_H_

#include <fuchsia/cpp/modular.h>

namespace modular {

std::ostream& operator<<(std::ostream& os, const modular::FocusedState& state);
std::ostream& operator<<(std::ostream& os, const modular::StoryMetadata& meta);
std::ostream& operator<<(std::ostream& os, const modular::ModuleMetadata& meta);
std::ostream& operator<<(std::ostream& os, const modular::EntityMetadata& meta);
std::ostream& operator<<(std::ostream& os, const modular::LinkMetadata& meta);
std::ostream& operator<<(std::ostream& os,
                         const modular::ContextMetadata& meta);

std::ostream& operator<<(std::ostream& os, const modular::ContextValue& value);
std::ostream& operator<<(std::ostream& os,
                         const modular::ContextSelector& selector);

std::ostream& operator<<(std::ostream& os,
                         const modular::ContextUpdate& update);
std::ostream& operator<<(std::ostream& os, const modular::ContextQuery& query);

}  // namespace modular

#endif  // LIB_CONTEXT_CPP_FORMATTING_H_
