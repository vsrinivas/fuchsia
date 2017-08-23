// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <string.h>
#include <sys/param.h>
#include <hid/usages.h>

#include "keyboard-vt100.h"
#include "keyboard.h"
#include "vc.h"

static struct list_node g_vc_list = LIST_INITIAL_VALUE(g_vc_list);
static unsigned g_vc_count = 0;
static unsigned g_active_vc_index;

vc_t* g_active_vc;
int g_status_width = 0;

// Process key sequences that affect the console (scrolling, switching
// console, etc.) without sending input to the current console.  This
// returns whether this key press was handled.
static bool vc_handle_control_keys(uint8_t keycode, int modifiers) {
    switch (keycode) {
    case HID_USAGE_KEY_F1 ... HID_USAGE_KEY_F10:
        if (modifiers & MOD_ALT) {
            vc_set_active(keycode - HID_USAGE_KEY_F1, NULL);
            return true;
        }
        break;

    case HID_USAGE_KEY_TAB:
        if (modifiers & MOD_ALT) {
            if (modifiers & MOD_SHIFT) {
                vc_set_active(g_active_vc_index == 0 ? g_vc_count - 1 : g_active_vc_index - 1, NULL);
            } else {
                vc_set_active(g_active_vc_index == g_vc_count - 1 ? 0 : g_active_vc_index + 1, NULL);
            }
            return true;
        }
        break;

    case HID_USAGE_KEY_UP:
        if (modifiers & MOD_ALT) {
            vc_scroll_viewport(g_active_vc, -1);
            return true;
        }
        break;
    case HID_USAGE_KEY_DOWN:
        if (modifiers & MOD_ALT) {
            vc_scroll_viewport(g_active_vc, 1);
            return true;
        }
        break;
    case HID_USAGE_KEY_PAGEUP:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport(g_active_vc, -(vc_rows(g_active_vc) / 2));
            return true;
        }
        break;
    case HID_USAGE_KEY_PAGEDOWN:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport(g_active_vc, vc_rows(g_active_vc) / 2);
            return true;
        }
        break;
    case HID_USAGE_KEY_HOME:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport_top(g_active_vc);
            return true;
        }
        break;
    case HID_USAGE_KEY_END:
        if (modifiers & MOD_SHIFT) {
            vc_scroll_viewport_bottom(g_active_vc);
            return true;
        }
        break;
    }
    return false;
}

// Process key sequences that affect the low-level control of the system
// (switching display ownership, rebooting).  This returns whether this key press
// was handled.
static bool vc_handle_device_control_keys(uint8_t keycode, int modifiers){
    switch (keycode) {
    case HID_USAGE_KEY_DELETE:
        // Provide a CTRL-ALT-DEL reboot sequence
        if ((modifiers & MOD_CTRL) && (modifiers & MOD_ALT)) {
            int fd;
            // Send the reboot command to devmgr
            if ((fd = open("/dev/misc/dmctl", O_WRONLY)) >= 0) {
                write(fd, "reboot", strlen("reboot"));
                close(fd);
            }
            return true;
        }
        break;

    case HID_USAGE_KEY_ESC:
        if (modifiers & MOD_ALT) {
            vc_toggle_framebuffer();
            return true;
        }
        break;
    }
    return false;
}

mx_status_t vc_set_active(int num, vc_t* to_vc) {
    vc_t* vc = NULL;
    int i = 0;
    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        if ((num == i) || (to_vc == vc)) {
            if (vc == g_active_vc) {
                return MX_OK;
            }
            if (g_active_vc) {
                g_active_vc->active = false;
                g_active_vc->flags &= ~VC_FLAG_HASOUTPUT;
            }
            vc->active = true;
            vc->flags &= ~VC_FLAG_HASOUTPUT;
            g_active_vc = vc;
            g_active_vc_index = i;
            vc_full_repaint(vc);
            vc_render(vc);
            return MX_OK;
        }
        i++;
    }
    return MX_ERR_NOT_FOUND;
}

void vc_status_update() {
    vc_t* vc = NULL;
    unsigned i = 0;
    int x = 0;

    int w = g_status_width / (g_vc_count + 1);
    if (w < MIN_TAB_WIDTH) {
        w = MIN_TAB_WIDTH;
    } else if (w > MAX_TAB_WIDTH) {
        w = MAX_TAB_WIDTH;
    }

    char tmp[w];

    vc_status_clear();
    list_for_every_entry (&g_vc_list, vc, vc_t, node) {
        unsigned fg;
        if (vc->active) {
            fg = STATUS_COLOR_ACTIVE;
        } else if (vc->flags & VC_FLAG_HASOUTPUT) {
            fg = STATUS_COLOR_UPDATED;
        } else {
            fg = STATUS_COLOR_DEFAULT;
        }

        int lines = vc_get_scrollback_lines(vc);
        char L = (lines > 0) && (-vc->viewport_y < lines) ? '<' : '[';
        char R = (vc->viewport_y < 0) ? '>' : ']';

        snprintf(tmp, w, "%c%u%c %s", L, i, R, vc->title);
        vc_status_write(x, fg, tmp);
        x += w;
        i++;
    }
}

void handle_key_press(uint8_t keycode, int modifiers) {
    // Handle vc-level control keys
    if (vc_handle_device_control_keys(keycode, modifiers))
        return;

    // Handle other keys only if we own the display
    if (!g_vc_owns_display)
        return;

    // Handle other control keys
    if (vc_handle_control_keys(keycode, modifiers))
        return;

    vc_t* vc = g_active_vc;
    char output[4];
    uint32_t length = hid_key_to_vt100_code(
        keycode, modifiers, vc->keymap, output, sizeof(output));
    if (length > 0) {
        if (vc->fd >= 0) {
            write(vc->fd, output, length);
        }
        vc_scroll_viewport_bottom(vc);
    }
}

ssize_t vc_write(vc_t* vc, const void* buf, size_t count, mx_off_t off) {
    vc->invy0 = vc_rows(vc) + 1;
    vc->invy1 = -1;
    const uint8_t* str = (const uint8_t*)buf;
    for (size_t i = 0; i < count; i++) {
        vc->textcon.putc(&vc->textcon, str[i]);
    }
    if (vc->invy1 >= 0) {
        int rows = vc_rows(vc);
        // Adjust for the current viewport position.  Convert
        // console-relative row numbers to screen-relative row numbers.
        int invalidate_y0 = MIN(vc->invy0 - vc->viewport_y, rows);
        int invalidate_y1 = MIN(vc->invy1 - vc->viewport_y, rows);
        vc_gfx_invalidate(vc, 0, invalidate_y0,
                          vc->columns, invalidate_y1 - invalidate_y0);
    }
    if (!(vc->flags & VC_FLAG_HASOUTPUT) && !vc->active) {
        vc->flags |= VC_FLAG_HASOUTPUT;
        vc_status_update();
        vc_gfx_invalidate_status();
    }
    return count;
}

// Create a new vc_t and add it to the console list.
mx_status_t vc_create(vc_t** vc_out, bool special) {
    mx_status_t status;
    vc_t* vc;
    if ((status = vc_alloc(&vc, special)) < 0) {
        return status;
    }

    // add to the vc list
    list_add_tail(&g_vc_list, &vc->node);
    g_vc_count++;

    // make this the active vc if it's the first one
    if (!g_active_vc) {
        vc_set_active(-1, vc);
    } else {
        vc_render(g_active_vc);
    }

    *vc_out = vc;
    return MX_OK;
}

void vc_destroy(vc_t* vc) {
    list_delete(&vc->node);
    g_vc_count -= 1;

    if (vc->active) {
        g_active_vc = NULL;
        if (g_active_vc_index >= g_vc_count) {
            g_active_vc_index = g_vc_count - 1;
        }
        vc_set_active(g_active_vc_index, NULL);
    } else if (g_active_vc) {
        vc_full_repaint(g_active_vc);
        vc_render(g_active_vc);
    }

    vc_free(vc);
}
