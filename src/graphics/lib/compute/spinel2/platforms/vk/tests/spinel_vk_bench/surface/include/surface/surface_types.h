// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_TYPES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_TYPES_H_

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

#ifdef __cplusplus
extern "C" {
#endif

//
//
//

struct surface;

//
//
//

struct surface_presentable
{
  // clang-format off
  struct
  {
    VkSemaphore  semaphore;      // image is ready
    VkFence      fence;          // image is ready
  } wait;
  VkSemaphore    signal;         // image is presentable
  VkSwapchainKHR swapchain;      // swapchain for this presentable
  VkImage        image;          // swapchain image
  VkImageView    image_view;     // swapchain image view
  uint32_t       image_index;    // index of swapchain image
  uint32_t       acquire_count;  // count of acquired
  void *         payload;        // payload from surface_acquire()
  // clang-format on
};

//
//
//

typedef enum surface_event_type_t
{
  SURFACE_EVENT_TYPE_NOOP,
  SURFACE_EVENT_TYPE_EXIT,
  SURFACE_EVENT_TYPE_KEYBOARD_PRESS,
  SURFACE_EVENT_TYPE_KEYBOARD_RELEASE,
  SURFACE_EVENT_TYPE_POINTER_INPUT,
  SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_V,
  SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_H,
  SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_PRESS,
  SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_RELEASE,
  SURFACE_EVENT_TYPE_TOUCH_INPUT,
  SURFACE_EVENT_TYPE_TOUCH_INPUT_BUTTON_PRESS,
  SURFACE_EVENT_TYPE_TOUCH_INPUT_BUTTON_RELEASE,
  SURFACE_EVENT_TYPE_TOUCH_INPUT_CONTACT_COUNT,
  SURFACE_EVENT_TYPE_STYLUS_INPUT,
  SURFACE_EVENT_TYPE_STYLUS_INPUT_BUTTON_PRESS,
  SURFACE_EVENT_TYPE_STYLUS_INPUT_BUTTON_RELEASE,
  SURFACE_EVENT_TYPE_EXPOSE,
  SURFACE_EVENT_TYPE_FOCUS_IN,
  SURFACE_EVENT_TYPE_FOCUS_OUT,
  SURFACE_EVENT_TYPE_POINTER_ENTER,
  SURFACE_EVENT_TYPE_POINTER_LEAVE,
} surface_event_type_t;

//
//
//

typedef enum surface_button_t
{
  SURFACE_BUTTON_1 = 0x01,
  SURFACE_BUTTON_2 = 0x02,
  SURFACE_BUTTON_3 = 0x04,
  SURFACE_BUTTON_4 = 0x08,
  SURFACE_BUTTON_5 = 0x10,
  SURFACE_BUTTON_6 = 0x20,
  SURFACE_BUTTON_7 = 0x40,
  SURFACE_BUTTON_8 = 0x80,
} surface_button_t;

//
//
//

union surface_buttons
{
  uint32_t dword;

  struct
  {
    uint32_t button_1 : 1;
    uint32_t button_2 : 1;
    uint32_t button_3 : 1;
    uint32_t button_4 : 1;
    uint32_t button_5 : 1;
    uint32_t button_6 : 1;
    uint32_t button_7 : 1;
    uint32_t button_8 : 1;
  };
};

struct surface_axis
{
  int64_t min;
  int64_t max;
};

struct surface_contact_axes
{
  struct surface_axis x;
  struct surface_axis y;
  struct surface_axis pressure;
  struct surface_axis width;
  struct surface_axis height;
};

struct surface_contact
{
  int64_t x;
  int64_t y;
  int64_t pressure;
  int64_t width;
  int64_t height;
};

//
//
//

struct surface_event
{
  surface_event_type_t type;
  uint32_t             device_id;
  uint64_t             timestamp;

  union
  {
    struct
    {
      uint32_t code;
    } keyboard;

    struct
    {
      VkExtent2D            extent;
      union surface_buttons buttons;
      int64_t               x;
      int64_t               y;
      int64_t               v;  // scroll
      int64_t               h;  // scroll
    } pointer;

    struct
    {
      VkExtent2D            extent;
      union surface_buttons buttons;

      struct
      {
        uint32_t prev;
        uint32_t curr;
      } contact_count;

      struct surface_contact      contacts[10];
      struct surface_contact_axes contact_axes;
    } touch;

    struct
    {
      //
      // NOTE(allanmac): Incomplete.  Not yet receiving reports and it's
      // likely there will be "axes" associated with this report.
      //
      VkExtent2D            extent;
      union surface_buttons buttons;
      int64_t               x;
      int64_t               y;
      int64_t               pressure;
      VkBool32              is_in_contact;
      VkBool32              is_in_range;
      VkBool32              is_inverted;
    } stylus;

    struct
    {
      // FIXME(allanmac): extent required?
      uint32_t x;
      uint32_t y;
      uint32_t width;
      uint32_t height;
    } expose;
  };
};

//
//
//

typedef void (*surface_input_pfn_t)(void * data, struct surface_event const * event);

//
// FIXME(allanmac): these were cut-and-pasted from "hid/usages.h".  Find
// a way to leave them where they are.
//
// These codes are defined in:
// "Universal Serial Bus HID Usage Tables"
// http://www.usb.org/developers/hidpage/Hut1_12v2.pdf
// (Version 1.12, 10/28/2004)
// See "Table 12: Keyboard/Keypad Page"
//

enum
{
  SURFACE_KEY_ERROR_ROLLOVER   = 0x01,
  SURFACE_KEY_POST_FAIL        = 0x02,
  SURFACE_KEY_ERROR_UNDEF      = 0x03,
  SURFACE_KEY_A                = 0x04,
  SURFACE_KEY_B                = 0x05,
  SURFACE_KEY_C                = 0x06,
  SURFACE_KEY_D                = 0x07,
  SURFACE_KEY_E                = 0x08,
  SURFACE_KEY_F                = 0x09,
  SURFACE_KEY_G                = 0x0a,
  SURFACE_KEY_H                = 0x0b,
  SURFACE_KEY_I                = 0x0c,
  SURFACE_KEY_J                = 0x0d,
  SURFACE_KEY_K                = 0x0e,
  SURFACE_KEY_L                = 0x0f,
  SURFACE_KEY_M                = 0x10,
  SURFACE_KEY_N                = 0x11,
  SURFACE_KEY_O                = 0x12,
  SURFACE_KEY_P                = 0x13,
  SURFACE_KEY_Q                = 0x14,
  SURFACE_KEY_R                = 0x15,
  SURFACE_KEY_S                = 0x16,
  SURFACE_KEY_T                = 0x17,
  SURFACE_KEY_U                = 0x18,
  SURFACE_KEY_V                = 0x19,
  SURFACE_KEY_W                = 0x1a,
  SURFACE_KEY_X                = 0x1b,
  SURFACE_KEY_Y                = 0x1c,
  SURFACE_KEY_Z                = 0x1d,
  SURFACE_KEY_1                = 0x1e,
  SURFACE_KEY_2                = 0x1f,
  SURFACE_KEY_3                = 0x20,
  SURFACE_KEY_4                = 0x21,
  SURFACE_KEY_5                = 0x22,
  SURFACE_KEY_6                = 0x23,
  SURFACE_KEY_7                = 0x24,
  SURFACE_KEY_8                = 0x25,
  SURFACE_KEY_9                = 0x26,
  SURFACE_KEY_0                = 0x27,
  SURFACE_KEY_ENTER            = 0x28,
  SURFACE_KEY_ESCAPE           = 0x29,
  SURFACE_KEY_BACKSPACE        = 0x2a,
  SURFACE_KEY_TAB              = 0x2b,
  SURFACE_KEY_SPACE            = 0x2c,
  SURFACE_KEY_MINUS            = 0x2d,
  SURFACE_KEY_EQUALS           = 0x2e,
  SURFACE_KEY_LEFT_BRACE       = 0x2f,
  SURFACE_KEY_RIGHT_BRACE      = 0x30,
  SURFACE_KEY_BACKSLASH        = 0x31,
  SURFACE_KEY_NON_US_HASH      = 0x32,
  SURFACE_KEY_SEMICOLON        = 0x33,
  SURFACE_KEY_APOSTROPHE       = 0x34,
  SURFACE_KEY_GRAVE_ACCENT     = 0x35,
  SURFACE_KEY_COMMA            = 0x36,
  SURFACE_KEY_DOT              = 0x37,
  SURFACE_KEY_SLASH            = 0x38,
  SURFACE_KEY_CAPS_LOCK        = 0x39,
  SURFACE_KEY_F1               = 0x3a,
  SURFACE_KEY_F2               = 0x3b,
  SURFACE_KEY_F3               = 0x3c,
  SURFACE_KEY_F4               = 0x3d,
  SURFACE_KEY_F5               = 0x3e,
  SURFACE_KEY_F6               = 0x3f,
  SURFACE_KEY_F7               = 0x40,
  SURFACE_KEY_F8               = 0x41,
  SURFACE_KEY_F9               = 0x42,
  SURFACE_KEY_F10              = 0x43,
  SURFACE_KEY_F11              = 0x44,
  SURFACE_KEY_F12              = 0x45,
  SURFACE_KEY_PRINT_SCREEN     = 0x46,
  SURFACE_KEY_SCROLL_LOCK      = 0x47,
  SURFACE_KEY_PAUSE            = 0x48,
  SURFACE_KEY_INSERT           = 0x49,
  SURFACE_KEY_HOME             = 0x4a,
  SURFACE_KEY_PAGE_UP          = 0x4b,
  SURFACE_KEY_DELETE           = 0x4c,
  SURFACE_KEY_END              = 0x4d,
  SURFACE_KEY_PAGE_DOWN        = 0x4e,
  SURFACE_KEY_RIGHT            = 0x4f,
  SURFACE_KEY_LEFT             = 0x50,
  SURFACE_KEY_DOWN             = 0x51,
  SURFACE_KEY_UP               = 0x52,
  SURFACE_KEY_NUM_LOCK         = 0x53,
  SURFACE_KEY_NON_US_BACKSLASH = 0x64,
  SURFACE_KEY_MENU             = 0x76,
  SURFACE_KEY_LEFT_CTRL        = 0xe0,
  SURFACE_KEY_LEFT_SHIFT       = 0xe1,
  SURFACE_KEY_LEFT_ALT         = 0xe2,
  SURFACE_KEY_LEFT_META        = 0xe3,
  SURFACE_KEY_RIGHT_CTRL       = 0xe4,
  SURFACE_KEY_RIGHT_SHIFT      = 0xe5,
  SURFACE_KEY_RIGHT_ALT        = 0xe6,
  SURFACE_KEY_RIGHT_META       = 0xe7,
  SURFACE_KEY_VOL_DOWN         = 0xe8,
  SURFACE_KEY_VOL_UP           = 0xe9,
  SURFACE_KEY_KEYPAD_SLASH     = 0x54,
  SURFACE_KEY_KEYPAD_ASTERISK  = 0x55,
  SURFACE_KEY_KEYPAD_MINUS     = 0x56,
  SURFACE_KEY_KEYPAD_PLUS      = 0x57,
  SURFACE_KEY_KEYPAD_ENTER     = 0x58,
  SURFACE_KEY_KEYPAD_1         = 0x59,
  SURFACE_KEY_KEYPAD_2         = 0x5a,
  SURFACE_KEY_KEYPAD_3         = 0x5b,
  SURFACE_KEY_KEYPAD_4         = 0x5c,
  SURFACE_KEY_KEYPAD_5         = 0x5d,
  SURFACE_KEY_KEYPAD_6         = 0x5e,
  SURFACE_KEY_KEYPAD_7         = 0x5f,
  SURFACE_KEY_KEYPAD_8         = 0x60,
  SURFACE_KEY_KEYPAD_9         = 0x61,
  SURFACE_KEY_KEYPAD_0         = 0x62,
  SURFACE_KEY_KEYPAD_DOT       = 0x63,
  SURFACE_KEY_KEYPAD_EQUALS    = 0x67,
};

//
//
//

#ifdef __cplusplus
}
#endif

//
//
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL2_PLATFORMS_VK_TESTS_SPINEL_VK_BENCH_SURFACE_INCLUDE_SURFACE_SURFACE_TYPES_H_
