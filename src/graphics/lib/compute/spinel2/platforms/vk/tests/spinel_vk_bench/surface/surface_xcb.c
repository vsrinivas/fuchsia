// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// See Chapter 33 of Window System Integration.
//
// Include <vulkan_core.h> and XCB headers before the platform-specific Vulkan
// WSI header.
//
#include "surface/surface_xcb.h"

#include <X11/keysym.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "surface_default.h"

//
// WSI
//
#include <vulkan/vulkan_xcb.h>

//
// from xcb_util.h
//

#ifndef XCB_EVENT_RESPONSE_TYPE_MASK
// clang-format off
#define XCB_EVENT_RESPONSE_TYPE_MASK (0x7f)
#define XCB_EVENT_RESPONSE_TYPE(e)   (e->response_type &  XCB_EVENT_RESPONSE_TYPE_MASK)
#define XCB_EVENT_SENT(e)            (e->response_type & ~XCB_EVENT_RESPONSE_TYPE_MASK)
// clang-format on
#endif

//
//
//

struct surface_platform
{
  // clang-format off
  xcb_connection_t *          connection;
  xcb_setup_t const *         setup;
  xcb_screen_t const *        screen;
  uint32_t                    xid;
  xcb_keysym_t                (*keysyms)[2]; // only Group 1 KeySyms

  struct
  {
    xcb_intern_atom_reply_t * delete;
  } reply;
  // clang-format on

  struct VkExtent2D extent;
};

//
//
//

static void
destroy(struct surface * surface)
{
  surface_detach(surface);

  vkDestroySurfaceKHR(surface->vk.i, surface->vk.surface, surface->vk.ac);

  struct surface_platform * const platform = surface->platform;

  xcb_destroy_window(platform->connection, platform->xid);
  xcb_disconnect(platform->connection);

  free(platform->reply.delete);
  free(platform->keysyms);

  free(platform);
  free(surface);
}

//
// convert Linux keycodes to X11 KeySyms
//

static void
create_keysyms(struct surface_platform * const platform)
{
  xcb_get_keyboard_mapping_cookie_t  //
    kmc = xcb_get_keyboard_mapping(platform->connection,
                                   platform->setup->min_keycode,
                                   platform->setup->max_keycode - platform->setup->min_keycode + 1);

  xcb_get_keyboard_mapping_reply_t *  //
    kmr = xcb_get_keyboard_mapping_reply(platform->connection, kmc, NULL);

  uint8_t const        keysyms_per_keycode = kmr->keysyms_per_keycode;
  xcb_keysym_t const * keysyms             = xcb_get_keyboard_mapping_keysyms(kmr);

  platform->keysyms = calloc(1, sizeof(*platform->keysyms) * (sizeof(xcb_keycode_t) << 8));

  for (int ii = platform->setup->min_keycode; ii <= platform->setup->max_keycode; ii++)
    {
      platform->keysyms[ii][0] = keysyms[0];
      platform->keysyms[ii][1] = keysyms[1];

      keysyms += keysyms_per_keycode;
    }

  // free reply
  free(kmr);
}

//
//
//

static uint32_t
get_keysym(struct surface_platform * const platform,
           xcb_keycode_t const             keycode,
           uint16_t const                  modifiers)
{
  static uint8_t const map[(XCB_KEY_BUT_MASK_SHIFT | XCB_KEY_BUT_MASK_LOCK) + 1] = {

    [XCB_KEY_BUT_MASK_SHIFT]                         = 1,
    [XCB_KEY_BUT_MASK_LOCK]                          = 1,
    [XCB_KEY_BUT_MASK_SHIFT | XCB_KEY_BUT_MASK_LOCK] = 0,
  };

  uint16_t const shift_lock = modifiers & (XCB_KEY_BUT_MASK_SHIFT | XCB_KEY_BUT_MASK_LOCK);

  return platform->keysyms[keycode][map[shift_lock]];
}

//
//
//

#define SURFACE_KEYSYM_MASK 0x1FF

#define SURFACE_KEYSYM_TO_HID_2(key_, hid_) [XK_##key_ & SURFACE_KEYSYM_MASK] = SURFACE_KEY_##hid_

#define SURFACE_KEYSYM_TO_HID(symbol_) SURFACE_KEYSYM_TO_HID_2(symbol_, symbol_)

static uint8_t const keysym_to_hid_map[] = {
  SURFACE_KEYSYM_TO_HID_2(a, A),
  SURFACE_KEYSYM_TO_HID_2(b, B),
  SURFACE_KEYSYM_TO_HID_2(c, C),
  SURFACE_KEYSYM_TO_HID_2(d, D),
  SURFACE_KEYSYM_TO_HID_2(e, E),
  SURFACE_KEYSYM_TO_HID_2(f, F),
  SURFACE_KEYSYM_TO_HID_2(g, G),
  SURFACE_KEYSYM_TO_HID_2(h, H),
  SURFACE_KEYSYM_TO_HID_2(i, I),
  SURFACE_KEYSYM_TO_HID_2(j, J),
  SURFACE_KEYSYM_TO_HID_2(k, K),
  SURFACE_KEYSYM_TO_HID_2(l, L),
  SURFACE_KEYSYM_TO_HID_2(m, M),
  SURFACE_KEYSYM_TO_HID_2(n, N),
  SURFACE_KEYSYM_TO_HID_2(o, O),
  SURFACE_KEYSYM_TO_HID_2(p, P),
  SURFACE_KEYSYM_TO_HID_2(q, Q),
  SURFACE_KEYSYM_TO_HID_2(r, R),
  SURFACE_KEYSYM_TO_HID_2(s, S),
  SURFACE_KEYSYM_TO_HID_2(t, T),
  SURFACE_KEYSYM_TO_HID_2(u, U),
  SURFACE_KEYSYM_TO_HID_2(v, V),
  SURFACE_KEYSYM_TO_HID_2(w, W),
  SURFACE_KEYSYM_TO_HID_2(x, X),
  SURFACE_KEYSYM_TO_HID_2(y, Y),
  SURFACE_KEYSYM_TO_HID_2(z, Z),
  SURFACE_KEYSYM_TO_HID(1),
  SURFACE_KEYSYM_TO_HID(2),
  SURFACE_KEYSYM_TO_HID(3),
  SURFACE_KEYSYM_TO_HID(4),
  SURFACE_KEYSYM_TO_HID(5),
  SURFACE_KEYSYM_TO_HID(6),
  SURFACE_KEYSYM_TO_HID(7),
  SURFACE_KEYSYM_TO_HID(8),
  SURFACE_KEYSYM_TO_HID(9),
  SURFACE_KEYSYM_TO_HID(0),
  SURFACE_KEYSYM_TO_HID_2(Return, ENTER),
  SURFACE_KEYSYM_TO_HID_2(Escape, ESCAPE),
  SURFACE_KEYSYM_TO_HID_2(BackSpace, BACKSPACE),
  SURFACE_KEYSYM_TO_HID_2(Tab, TAB),
  SURFACE_KEYSYM_TO_HID_2(space, SPACE),
  SURFACE_KEYSYM_TO_HID_2(minus, MINUS),
  SURFACE_KEYSYM_TO_HID_2(equal, EQUALS),
  SURFACE_KEYSYM_TO_HID_2(braceleft, LEFT_BRACE),
  SURFACE_KEYSYM_TO_HID_2(braceright, RIGHT_BRACE),
  SURFACE_KEYSYM_TO_HID_2(backslash, BACKSLASH),
  SURFACE_KEYSYM_TO_HID_2(asciitilde, NON_US_HASH),
  SURFACE_KEYSYM_TO_HID_2(semicolon, SEMICOLON),
  SURFACE_KEYSYM_TO_HID_2(apostrophe, APOSTROPHE),
  SURFACE_KEYSYM_TO_HID_2(grave, GRAVE_ACCENT),
  SURFACE_KEYSYM_TO_HID_2(comma, COMMA),
  SURFACE_KEYSYM_TO_HID_2(period, DOT),
  SURFACE_KEYSYM_TO_HID_2(slash, SLASH),
  SURFACE_KEYSYM_TO_HID_2(Caps_Lock, CAPS_LOCK),
  SURFACE_KEYSYM_TO_HID(F1),
  SURFACE_KEYSYM_TO_HID(F2),
  SURFACE_KEYSYM_TO_HID(F3),
  SURFACE_KEYSYM_TO_HID(F4),
  SURFACE_KEYSYM_TO_HID(F5),
  SURFACE_KEYSYM_TO_HID(F6),
  SURFACE_KEYSYM_TO_HID(F7),
  SURFACE_KEYSYM_TO_HID(F8),
  SURFACE_KEYSYM_TO_HID(F9),
  SURFACE_KEYSYM_TO_HID(F10),
  SURFACE_KEYSYM_TO_HID(F11),
  SURFACE_KEYSYM_TO_HID(F12),
  SURFACE_KEYSYM_TO_HID_2(Print, PRINT_SCREEN),
  SURFACE_KEYSYM_TO_HID_2(Scroll_Lock, SCROLL_LOCK),
  SURFACE_KEYSYM_TO_HID_2(Pause, PAUSE),
  SURFACE_KEYSYM_TO_HID_2(Insert, INSERT),
  SURFACE_KEYSYM_TO_HID_2(Home, HOME),
  SURFACE_KEYSYM_TO_HID_2(Page_Up, PAGE_UP),
  SURFACE_KEYSYM_TO_HID_2(Delete, DELETE),
  SURFACE_KEYSYM_TO_HID_2(End, END),
  SURFACE_KEYSYM_TO_HID_2(Page_Down, PAGE_DOWN),
  SURFACE_KEYSYM_TO_HID_2(Right, RIGHT),
  SURFACE_KEYSYM_TO_HID_2(Left, LEFT),
  SURFACE_KEYSYM_TO_HID_2(Down, DOWN),
  SURFACE_KEYSYM_TO_HID_2(Up, UP),
  SURFACE_KEYSYM_TO_HID_2(bar, NON_US_BACKSLASH),
  SURFACE_KEYSYM_TO_HID_2(Control_L, LEFT_CTRL),
  SURFACE_KEYSYM_TO_HID_2(Shift_L, LEFT_SHIFT),
  SURFACE_KEYSYM_TO_HID_2(Alt_L, LEFT_ALT),
  SURFACE_KEYSYM_TO_HID_2(Meta_L, LEFT_META),
  SURFACE_KEYSYM_TO_HID_2(Control_R, RIGHT_CTRL),
  SURFACE_KEYSYM_TO_HID_2(Shift_R, RIGHT_SHIFT),
  SURFACE_KEYSYM_TO_HID_2(Alt_R, RIGHT_ALT),
  SURFACE_KEYSYM_TO_HID_2(Meta_R, RIGHT_META),
  SURFACE_KEYSYM_TO_HID_2(Menu, MENU),
};

//
// For now we want to know if this changes
//

STATIC_ASSERT_MACRO_1(ARRAY_LENGTH_MACRO(keysym_to_hid_map) <= 512);

//
//
//

static uint32_t
key_to_hid(uint32_t keysym)
{
  uint32_t const keysym_masked = (keysym & SURFACE_KEYSYM_MASK);

  if (keysym_masked < ARRAY_LENGTH_MACRO(keysym_to_hid_map))
    {
      return keysym_to_hid_map[keysym_masked];
    }
  else
    {
      return 0;
    }
}

//
// DEBUG: print modifiers
//

#if 0

static void
print_modifiers(uint32_t mask)
{
  // clang-format off
#define SURFACE_XCB_KEY_MODIFIERS()                                 \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_SHIFT    , "Shift"   )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_LOCK     , "Lock"    )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_CONTROL  , "Control" )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_MOD_1    , "Mod_1"   )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_MOD_2    , "Mod_2"   )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_MOD_3    , "Mod_3"   )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_MOD_4    , "Mod_4"   )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_MOD_5    , "Mod_5"   )  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_BUTTON_1 , "Button_1")  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_BUTTON_2 , "Button_2")  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_BUTTON_3 , "Button_3")  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_BUTTON_4 , "Button_4")  \
  SURFACE_XCB_KEY_MODIFIER(XCB_KEY_BUT_MASK_BUTTON_5 , "Button_5")
  // clang-format on

#undef SURFACE_XCB_KEY_MODIFIER
#define SURFACE_XCB_KEY_MODIFIER(mask_, name_)                                                     \
  if (mask & mask_)                                                                                \
    {                                                                                              \
      fprintf(stderr, "%8s ", name_);                                                              \
    }

  SURFACE_XCB_KEY_MODIFIERS()

  fprintf(stderr, "\n");
}

#endif

//
// DEBUG: print XCB response type
//
// #define SURFACE_XCB_DEBUG_EVENT

#if !defined(NDEBUG) && defined(SURFACE_XCB_DEBUG_EVENT)

static char const *
print_event_response_type(uint8_t const type)
{
#define SURFACE_XCB_EVENT_RESPONSE_TYPES()                                                         \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_KEY_PRESS)                                                   \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_KEY_RELEASE)                                                 \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_BUTTON_PRESS)                                                \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_BUTTON_RELEASE)                                              \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_MOTION_NOTIFY)                                               \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_ENTER_NOTIFY)                                                \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_LEAVE_NOTIFY)                                                \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_FOCUS_IN)                                                    \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_FOCUS_OUT)                                                   \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_KEYMAP_NOTIFY)                                               \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_EXPOSE)                                                      \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_GRAPHICS_EXPOSURE)                                           \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_NO_EXPOSURE)                                                 \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_VISIBILITY_NOTIFY)                                           \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_CREATE_NOTIFY)                                               \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_DESTROY_NOTIFY)                                              \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_UNMAP_NOTIFY)                                                \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_MAP_NOTIFY)                                                  \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_MAP_REQUEST)                                                 \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_REPARENT_NOTIFY)                                             \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_CONFIGURE_NOTIFY)                                            \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_CONFIGURE_REQUEST)                                           \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_GRAVITY_NOTIFY)                                              \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_RESIZE_REQUEST)                                              \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_CIRCULATE_NOTIFY)                                            \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_CIRCULATE_REQUEST)                                           \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_PROPERTY_NOTIFY)                                             \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_SELECTION_CLEAR)                                             \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_SELECTION_REQUEST)                                           \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_SELECTION_NOTIFY)                                            \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_COLORMAP_NOTIFY)                                             \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_CLIENT_MESSAGE)                                              \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_MAPPING_NOTIFY)                                              \
  SURFACE_XCB_EVENT_RESPONSE_TYPE(XCB_GE_GENERIC)

  switch (type)
    {
#undef SURFACE_XCB_EVENT_RESPONSE_TYPE
#define SURFACE_XCB_EVENT_RESPONSE_TYPE(symbol_)                                                   \
  case symbol_:                                                                                    \
    return #symbol_;

      SURFACE_XCB_EVENT_RESPONSE_TYPES()

      default:
        return "Unknown XCB event response type";
    }
}

#endif

//
//
//

static void
input(struct surface * surface, surface_input_pfn_t input_pfn, void * data)
{
  struct surface_platform * const platform = surface->platform;

  // drain all events
  while (true)
    {
      xcb_generic_event_t * gev = xcb_poll_for_event(platform->connection);

      if (gev == NULL)
        return;

      uint8_t const ert = XCB_EVENT_RESPONSE_TYPE(gev);

      switch (ert)
        {
            case XCB_CLIENT_MESSAGE: {
              xcb_client_message_event_t const * cm = (xcb_client_message_event_t *)gev;

              if (cm->data.data32[0] == platform->reply.delete->atom)
                {
                  struct surface_event const event = { .type = SURFACE_EVENT_TYPE_EXIT };

                  input_pfn(data, &event);
                }
              break;
            }
            case XCB_EXPOSE: {
              xcb_expose_event_t const * ev = (xcb_expose_event_t *)gev;

              struct surface_event const event = { .type          = SURFACE_EVENT_TYPE_EXPOSE,
                                                   .expose.x      = ev->x,
                                                   .expose.y      = ev->y,
                                                   .expose.width  = ev->width,
                                                   .expose.height = ev->height };
              input_pfn(data, &event);
              break;
            }
            case XCB_FOCUS_IN: {
              struct surface_event const event = { .type = SURFACE_EVENT_TYPE_FOCUS_IN };

              input_pfn(data, &event);
              break;
            }
            case XCB_FOCUS_OUT: {
              struct surface_event const event = { .type = SURFACE_EVENT_TYPE_FOCUS_OUT };

              input_pfn(data, &event);
              break;
            }
            case XCB_BUTTON_PRESS: {
              xcb_button_press_event_t const * ev = (xcb_button_press_event_t *)gev;

              struct surface_event event = {
                .pointer.extent = platform->extent,
                .pointer.x      = ev->event_x,
                .pointer.y      = ev->event_y,
              };

              switch (ev->detail)
                {
                  case 1:
                  case 2:
                  case 3:
                    event.type                  = SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_PRESS;
                    event.pointer.buttons.dword = 1 << (ev->detail - 1);
                    input_pfn(data, &event);
                    break;

                  case 4:
                  case 5:
                    event.type      = SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_V;
                    event.pointer.v = (ev->detail == XCB_BUTTON_INDEX_4) ? +1 : -1;
                    input_pfn(data, &event);
                    break;

                  case 6:
                  case 7:
                    event.type      = SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_H;
                    event.pointer.h = (ev->detail == 6) ? +1 : -1;  // no enum available
                    input_pfn(data, &event);
                    break;

                  default:  // ignore
                    break;
                }
              break;
            }
            case XCB_BUTTON_RELEASE: {
              xcb_button_release_event_t const * ev = (xcb_button_release_event_t *)gev;

              switch (ev->detail)
                {
                  // clang-format off
                  case 1:
                  case 2:
                  case 3:  {
                    struct surface_event const event = {

                      .type                  = SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_RELEASE,
                      .pointer.extent        = platform->extent,
                      .pointer.buttons.dword = 1 << (ev->detail - 1),
                      .pointer.x             = ev->event_x,
                      .pointer.y             = ev->event_y,
                    };

                    input_pfn(data, &event);
                    break;
                  }
                  // clang-format on
                  default:
                    break;
                }
              break;
            }
            case XCB_MOTION_NOTIFY: {
              xcb_motion_notify_event_t const * ev = (xcb_motion_notify_event_t *)gev;

              struct surface_event const event = {
                .type                  = SURFACE_EVENT_TYPE_POINTER_INPUT,
                .pointer.extent        = platform->extent,
                .pointer.buttons.dword = ev->state / XCB_BUTTON_MASK_1,
                .pointer.x             = ev->event_x,
                .pointer.y             = ev->event_y,
              };

              input_pfn(data, &event);
              break;
            }
            case XCB_ENTER_NOTIFY: {
              xcb_enter_notify_event_t const * ev = (xcb_enter_notify_event_t *)gev;

              struct surface_event const event = {
                .type                  = SURFACE_EVENT_TYPE_POINTER_ENTER,
                .pointer.extent        = platform->extent,
                .pointer.buttons.dword = ev->state,
                .pointer.x             = ev->event_x,
                .pointer.y             = ev->event_y,
              };

              input_pfn(data, &event);
              break;
            }
            case XCB_LEAVE_NOTIFY: {
              xcb_leave_notify_event_t const * ev = (xcb_leave_notify_event_t *)gev;

              struct surface_event const event = {
                .type                  = SURFACE_EVENT_TYPE_POINTER_LEAVE,
                .pointer.extent        = platform->extent,
                .pointer.buttons.dword = ev->state,
                .pointer.x             = ev->event_x,
                .pointer.y             = ev->event_y,
              };

              input_pfn(data, &event);
              break;
            }
            case XCB_KEY_PRESS: {
              xcb_key_press_event_t const * ev = (xcb_key_press_event_t *)gev;

              struct surface_event const event = {
                .type          = SURFACE_EVENT_TYPE_KEYBOARD_PRESS,
                .keyboard.code = key_to_hid(get_keysym(platform, ev->detail, 0 /*ev->state*/))
              };

              input_pfn(data, &event);
              break;
            }
            case XCB_KEY_RELEASE: {
              xcb_key_release_event_t const * ev = (xcb_key_release_event_t *)gev;

              struct surface_event const event = {
                .type          = SURFACE_EVENT_TYPE_KEYBOARD_RELEASE,
                .keyboard.code = key_to_hid(get_keysym(platform, ev->detail, 0 /*ev->state*/))
              };

              input_pfn(data, &event);
              break;
            }
            default: {
#if !defined(NDEBUG) && defined(SURFACE_XCB_DEBUG_EVENT)
              fprintf(stderr, "Unhandled: %s\n", print_event_response_type(ert));
#endif
              break;
            }
        }

      free(gev);
    }
}

//
//
//

static VkResult
regen(struct surface * surface, VkExtent2D * extent, uint32_t * image_count)
{
  VkResult const result = surface_default_regen(surface, &surface->platform->extent, image_count);

  if ((extent != NULL) && (result == VK_SUCCESS))
    {
      *extent = surface->platform->extent;
    }

  return result;
}

//
//
//

struct surface *
surface_xcb_create(VkInstance                    vk_i,  //
                   VkAllocationCallbacks const * vk_ac,
                   VkRect2D const *              win_size,
                   char const *                  win_title)
{
  //
  // is XCB WSI layer present?
  //
  PFN_vkCreateXcbSurfaceKHR const pfn_vkCreateXcbSurfaceKHR =
    (PFN_vkCreateXcbSurfaceKHR)vkGetInstanceProcAddr(vk_i, "vkCreateXcbSurfaceKHR");

  if (pfn_vkCreateXcbSurfaceKHR == NULL)
    {
      fprintf(stderr, "Error: vkGetInstanceProcAddr(\"vkCreateXcbSurfaceKHR\") = NULL\n");

      return NULL;
    }

  //
  // connect using the $DISPLAY environment variable
  //
  int                screen_count;
  xcb_connection_t * connection;

  connection = xcb_connect(NULL, &screen_count);

  int const xcb_err = xcb_connection_has_error(connection);

  if (xcb_err > 0)
    {
      fprintf(stderr, "Error: xcb_connection_has_error() = %d\n", xcb_err);

      return NULL;
    }

  //
  // surface
  //
  struct surface * const surface = malloc(sizeof(*surface));

  surface->vk.i       = vk_i;
  surface->vk.ac      = vk_ac;
  surface->vk.surface = VK_NULL_HANDLE;
  surface->device     = NULL;

  surface->to_vk      = surface_default_to_vk;
  surface->destroy    = destroy;
  surface->attach     = surface_default_attach;
  surface->detach     = surface_default_detach;
  surface->regen      = regen;
  surface->next_fence = surface_default_next_fence;
  surface->acquire    = surface_default_acquire;
  surface->input      = input;

  //
  // platform
  //
  struct surface_platform * const platform = malloc(sizeof(*platform));

  surface->platform = platform;

  // zero
  platform->extent = (VkExtent2D){ 0, 0 };

  // save
  platform->connection = connection;
  platform->setup      = xcb_get_setup(platform->connection);

  // get keycode>keysym mapping
  create_keysyms(platform);

  // get screen 0
  xcb_screen_iterator_t iter = xcb_setup_roots_iterator(platform->setup);

  while (screen_count-- > 0)
    {
      xcb_screen_next(&iter);
    }

  platform->screen = iter.data;

  // get window's id
  platform->xid = xcb_generate_id(platform->connection);

  // register event types
  uint32_t const value_mask   = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  uint32_t const value_list[] = {

    platform->screen->white_pixel,
    // clang-format off
    //
    (
      XCB_EVENT_MASK_KEY_PRESS                  |
      XCB_EVENT_MASK_KEY_RELEASE                |
      XCB_EVENT_MASK_BUTTON_PRESS               |
      XCB_EVENT_MASK_BUTTON_RELEASE             |
      XCB_EVENT_MASK_ENTER_WINDOW               |
      XCB_EVENT_MASK_LEAVE_WINDOW               |
      XCB_EVENT_MASK_POINTER_MOTION             |
      // XCB_EVENT_MASK_POINTER_MOTION_HINT     |
      // XCB_EVENT_MASK_BUTTON_1_MOTION         |
      // XCB_EVENT_MASK_BUTTON_2_MOTION         |
      // XCB_EVENT_MASK_BUTTON_3_MOTION         |
      // XCB_EVENT_MASK_BUTTON_4_MOTION         |
      // XCB_EVENT_MASK_BUTTON_5_MOTION         |
      // XCB_EVENT_MASK_BUTTON_MOTION           |
      // XCB_EVENT_MASK_KEYMAP_STATE            |
      XCB_EVENT_MASK_EXPOSURE                   |
      XCB_EVENT_MASK_VISIBILITY_CHANGE          |
      XCB_EVENT_MASK_STRUCTURE_NOTIFY           |
      // XCB_EVENT_MASK_RESIZE_REDIRECT         |
      // XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY     |
      // XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT   |
      XCB_EVENT_MASK_FOCUS_CHANGE               |
      // XCB_EVENT_MASK_PROPERTY_CHANGE         |
      // XCB_EVENT_MASK_COLOR_MAP_CHANGE        |
      // XCB_EVENT_MASK_OWNER_GRAB_BUTTON
      //
      XCB_EVENT_MASK_NO_EVENT
    )
    // clang-format on
  };

  // create the window
  xcb_create_window(platform->connection,
                    XCB_COPY_FROM_PARENT,
                    platform->xid,
                    platform->screen->root,
                    0,
                    0,
                    (unsigned short)win_size->extent.width,
                    (unsigned short)win_size->extent.height,
                    0,
                    XCB_WINDOW_CLASS_INPUT_OUTPUT,
                    platform->screen->root_visual,
                    value_mask,
                    value_list);

  // set window position
  xcb_configure_window(platform->connection,
                       platform->xid,
                       XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
                       (uint32_t[]){ win_size->offset.x, win_size->offset.y });

  // set window title
  xcb_change_property(platform->connection,
                      XCB_PROP_MODE_REPLACE,
                      platform->xid,
                      XCB_ATOM_WM_NAME,
                      XCB_ATOM_STRING,
                      8,
                      (uint32_t)strlen(win_title),
                      win_title);

  // set icon title
  xcb_change_property(platform->connection,
                      XCB_PROP_MODE_REPLACE,
                      platform->xid,
                      XCB_ATOM_WM_ICON_NAME,
                      XCB_ATOM_STRING,
                      8,
                      (uint32_t)strlen(win_title),
                      win_title);

  // ICCCM: send notification when window is destroyed
  xcb_intern_atom_cookie_t cookies[] = {
    xcb_intern_atom(platform->connection, false, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS"),
    xcb_intern_atom(platform->connection, false, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW"),
  };

  xcb_intern_atom_reply_t * reply_protocols = xcb_intern_atom_reply(platform->connection,  //
                                                                    cookies[0],
                                                                    NULL);

  platform->reply.delete = xcb_intern_atom_reply(platform->connection,  //
                                                 cookies[1],
                                                 NULL);

  xcb_change_property(platform->connection,
                      XCB_PROP_MODE_REPLACE,
                      platform->xid,
                      reply_protocols->atom,
                      XCB_ATOM_ATOM,
                      32,
                      1,
                      &platform->reply.delete->atom);

  free(reply_protocols);

  // create the surface
  VkXcbSurfaceCreateInfoKHR const xcb_sci = {

    .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
    .pNext      = NULL,
    .flags      = 0,
    .connection = platform->connection,
    .window     = platform->xid
  };

  vk_ok(pfn_vkCreateXcbSurfaceKHR(vk_i, &xcb_sci, vk_ac, &surface->vk.surface));

  // map the window
  xcb_map_window(platform->connection, platform->xid);

  // flush to present the window
  xcb_flush(platform->connection);

  return surface;
}

//
//
//
