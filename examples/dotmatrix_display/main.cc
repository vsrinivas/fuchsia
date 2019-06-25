// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int CreateLogo();
int RunInvaders(void);

void PrintHelp() {
  printf(
      "Usage: dotmatrix_display {logo,invaders} \n \
      dotmatrix_display: This program opens a dotmatrix display and draws something on it \n \
         at the moment the programs are hard-wired for the display configuration of the \n \
         ssd1306 display. They will gracefully fail if given another display configuration \n \
         \n \
      - logo: Passing the option 'logo' will print the fuchsia logo on the display \n \
      - invaders: Passing the option 'invaders' will animate space invaders graphics on the \n \
          display.\n");
}

int main(int argc, char** argv) {
  if (argc > 1) {
    if (strcmp(argv[1], "logo") == 0) {
      return CreateLogo();
    }
    if (strcmp(argv[1], "invaders") == 0) {
      return RunInvaders();
    }
  }
  PrintHelp();
  return 0;
}
