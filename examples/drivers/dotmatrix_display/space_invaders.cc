// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/dotmatrixdisplay/c/fidl.h>
#include <fuchsia/hardware/ftdi/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <filesystem>
#include <iostream>
#include <vector>

constexpr int kWidth = 128;
constexpr int kHeight = 64;

std::vector<uint8_t> frame_buffer_(kWidth * 8);

void ClearScreen() {
  for (size_t i = 0; i < 8; i++) {
    for (size_t j = 0; j < kWidth; j++) {
      frame_buffer_[i * kWidth + j] = 0;
    }
  }
}
void SetPixel(uint32_t x, uint32_t y) {
  int row = y / 8;
  int offset = y % 8;

  uint8_t data = frame_buffer_[row * kWidth + x];
  uint8_t mask = static_cast<uint8_t>(1 << offset);
  frame_buffer_[row * kWidth + x] = static_cast<uint8_t>(data | mask);
}

class Invader {
 public:
  static constexpr int kXSize = 11;
  static constexpr int kYSize = 7;

  Invader(int x, int y) {
    x_ = x;
    y_ = y;
  }

  void Update(int rel_x, int rel_y) {
    x_ += rel_x;
    y_ += rel_y;
  }

  void Draw() {
    int x_vals[] = {3, 9,  4, 8, 3, 4,  5, 6, 7, 8,  9, 2,  3,  5, 6, 7,
                    9, 10, 1, 2, 3, 4,  5, 6, 7, 8,  9, 10, 11, 1, 3, 4,
                    5, 6,  7, 8, 9, 11, 1, 3, 9, 11, 4, 5,  7,  8

    };
    int y_vals[] = {0, 0, 1, 1, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,
                    3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5,
                    5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7};

    for (size_t i = 0; i < sizeof(x_vals) / sizeof(*x_vals); i++) {
      SetPixel(x_ + x_vals[i], y_ + y_vals[i]);
    }
  }

 private:
  int x_ = 0;
  int y_ = 0;
};

class InvaderBlock {
 public:
  static constexpr int kBlockWidth = kWidth - 30;
  static constexpr int kHeightJump = 3;
  static constexpr int kNumRows = 3;
  static constexpr int kBlockHeight = (Invader::kYSize + 3) * kNumRows;

  InvaderBlock() {
    for (int rows = 0; rows < 3; rows++) {
      for (int i = 0; i < kBlockWidth / Invader::kXSize; i++) {
        Invader inv(i * (Invader::kXSize + 1), rows * (Invader::kYSize + 3));
        invaders_.push_back(std::move(inv));
      }
    }
  }

  void UpdateAndDraw() {
    int rel_x = 0;
    int rel_y = 0;
    if (!IsTurnAround()) {
      rel_x = x_jump_;
    } else {
      rel_y += kHeightJump;
      x_jump_ *= -1;
      if (y_ + kBlockHeight + 5 >= kHeight) {
        rel_x = -1 * x_;
        rel_y = -1 * y_;
      }
    }
    x_ += rel_x;
    y_ += rel_y;
    for (Invader& i : invaders_) {
      i.Update(rel_x, rel_y);
      i.Draw();
    }
  }

 private:
  bool IsTurnAround() const {
    if (x_jump_ > 0) {
      return (x_ + kBlockWidth) >= kWidth;
    } else {
      return (x_ <= 0);
    }
  }

  int x_ = 0;
  int y_ = 0;
  int x_jump_ = 1;
  std::vector<Invader> invaders_;
};

class Player {
 public:
  static constexpr int kBlockWidth = 8;
  void Draw() {
    int x_vals[] = {
        4, 3, 5, 2, 6, 1, 7, 0, 8,

    };
    int y_vals[] = {
        0, 1, 1, 2, 2, 3, 3, 4, 4,
    };

    for (size_t i = 0; i < sizeof(x_vals) / sizeof(*x_vals); i++) {
      SetPixel(x_ + x_vals[i], y_ + y_vals[i]);
    }
  }

  void UpdateAndDraw() {
    x_ += x_jump_;
    int rand = std::rand() % 20;
    if (rand == 0) {
      x_jump_ *= -1;
    }
    if (IsTurnAround()) {
      x_jump_ *= -1;
    }
    Draw();
  }

 private:
  bool IsTurnAround() {
    if (x_jump_ > 0) {
      return (x_ + kBlockWidth) >= kWidth;
    } else {
      return (x_ <= 0);
    }
  }
  int x_jump_ = 1;
  int x_ = 0;
  int y_ = kHeight - 5;
};

int RunInvaders() {
  const char* path = "/dev/class/dotmatrix-display/";
  int fd_display = -1;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    fd_display = open(entry.path().c_str(), O_RDWR);
    if (fd_display > 0) {
      break;
    }
  }
  if (fd_display <= 0) {
    printf("Open dotmatrix-display failed with %d\n", fd_display);
    return 1;
  }

  zx_handle_t handle_display;
  zx_status_t status = fdio_get_service_handle(fd_display, &handle_display);
  if (status != ZX_OK) {
    printf("Ftdio get handle failed with %d\n", status);
    return 1;
  }

  fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplayConfig display_config;
  fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplayGetConfig(handle_display,
                                                              &display_config);
  if (display_config.width != kWidth || display_config.height != kHeight ||
      display_config.format !=
          fuchsia_hardware_dotmatrixdisplay_PixelFormat_MONOCHROME ||
      display_config.layout !=
          fuchsia_hardware_dotmatrixdisplay_ScreenLayout_COLUMN_TB_ROW_LR) {
    printf("Error: Display configs do not match supported config\n");
    printf("Width:  Support: %d Recieved: %d \n", kWidth, display_config.width);
    printf("Height: Support: %d Recieved: %d \n", kHeight,
           display_config.height);
    printf("Format: Support: %d Recieved: %d \n",
           fuchsia_hardware_dotmatrixdisplay_PixelFormat_MONOCHROME,
           display_config.format);
    printf("Layout: Support: %d Recieved: %d \n",
           fuchsia_hardware_dotmatrixdisplay_ScreenLayout_COLUMN_TB_ROW_LR,
           display_config.layout);
    return 1;
  }

  InvaderBlock invBlock;
  Player player;
  Invader inv = Invader(0, 0);

  while (true) {
    ClearScreen();
    invBlock.UpdateAndDraw();
    player.UpdateAndDraw();

    fuchsia_hardware_dotmatrixdisplay_DotmatrixDisplaySetScreen(
        handle_display, frame_buffer_.data(), frame_buffer_.size(), &status);
    if (status != ZX_OK) {
      printf("Display SetScreen failed with %d\n", status);
      return 1;
    }
  }
  return 0;
}
