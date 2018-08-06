#include "display.h"
#include "lib/fxl/logging.h"

#include <fcntl.h>
#include <zircon/device/backlight.h>

namespace display {

#define DEVICE_PATH "/dev/class/backlight/000"
#define BRIGHTNESS_BASE 255;

Display::Display(int fd) : fd_{fd} {}

Display::~Display() {}

Display* Display::GetDisplay() {
  const int fd = open(DEVICE_PATH, O_RDWR);

  if (fd < 0) {
    FXL_LOG(ERROR) << "Failed to open backlight";
    return NULL;
  }

  return new Display(fd);
}

bool Display::GetBrightness(double* brightness) {
  backlight_state_t state;
  const ssize_t ret = ioctl_backlight_get_state(fd_, &state);

  if (ret < 0) {
    FXL_LOG(ERROR) << "Getting backlight state ioctl failed";
    return false;
  }

  *brightness = (state.brightness * 1.0f) / BRIGHTNESS_BASE;
  return true;
}

bool Display::SetBrightness(double brightness) {
  const uint32_t adjustBrightness = brightness * BRIGHTNESS_BASE;
  backlight_state_t state = {.on = brightness > 0,
                             .brightness = (uint8_t)adjustBrightness};
  const ssize_t ret = ioctl_backlight_set_state(fd_, &state);

  if (ret < 0) {
    FXL_LOG(ERROR) << "Set brightness ioctl failed";
    return false;
  }

  return true;
}

}  // namespace display