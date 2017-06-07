// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fstream>

#include "tokens.h"

class Node;
class Tokenizer;

bool process_file(Tokenizer* container, const char* in_path, Node& root);
bool print_header_file(std::ofstream& os);
