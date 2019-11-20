// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "src/lib/line_input/line_input.h"

line_input::LineInput* g_line_input = nullptr;
bool g_should_quit = false;

void OnAccept(const std::string& line) {
  if (line == "quit") {
    g_should_quit = true;
    g_line_input->Hide();  // Always hide before quitting to put the terminal back.
  } else {
    printf("Got the input:\n  %s\n", line.c_str());
    g_line_input->AddToHistory(line);
  }
}

void OnEof() {
  g_should_quit = true;
  g_line_input->Hide();  // Always hide before quitting to put the terminal back.
}

int main(int argc, char** argv) {
  line_input::LineInputStdout input(&OnAccept, "C:\\> ");
  input.SetEofCallback(&OnEof);
  g_line_input = &input;

  printf(
      "Type some lines, nonempty lines will be added to history.\n"
      "\"quit\" or Control-D will exit.\n");

  input.Show();

  // This example does simple blocking input.
  while (!g_should_quit)
    input.OnInput(getc(stdin));

  return 0;
}
