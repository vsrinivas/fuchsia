// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "surface/surface_fuchsia_fb.h"

#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/component/cpp/incoming/service_client.h>
#include <lib/fdio/fdio.h>
#include <lib/trace-provider/provider.h>
#include <vulkan/vulkan_fuchsia.h>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <filesystem>
#include <string_view>

#include "common/macros.h"
#include "common/vk/assert.h"
#include "surface_default.h"
#include "surface_fuchsia_key_to_hid.h"

//
//
//
namespace FIR = fuchsia_input_report;

//
// Ensure that the surface event structs are at least as large as Fuchsia's
//
static_assert(MEMBER_SIZE_MACRO(struct surface_event, pointer.buttons.dword) * 32 >=
              FIR::wire::kMouseMaxNumButtons);

static_assert(MEMBER_SIZE_MACRO(struct surface_event, touch.contacts) >=
              FIR::wire::kTouchMaxContacts);

static_assert(MEMBER_SIZE_MACRO(struct surface_event, touch.buttons.dword) * 32 >=
              FIR::wire::kTouchMaxNumButtons);

//
//
//
struct surface_platform;

//
//
//
class reader_ctx : public fidl::WireResponseContext<FIR::InputReportsReader::ReadInputReports> {
  //
  //
  //
  struct surface_platform * const                   platform;
  uint32_t const                                    device_id;
  fidl::WireResult<FIR::InputDevice::GetDescriptor> descriptor;
  fidl::WireClient<FIR::InputReportsReader>         reader;

  //
  //
  //
 public:
  reader_ctx(struct surface_platform *                   platform,
             uint32_t                                    device_id,
             fidl::WireSyncClient<FIR::InputDevice> &    input_device,
             fidl::WireClient<FIR::InputReportsReader> & input_reader)
      : platform(platform),
        device_id(device_id),
        descriptor(input_device->GetDescriptor()),  // save the result
        reader(std::move(input_reader))             // take ownership of the reader
  {
    fidl::AsyncClientBuffer<FIR::InputReportsReader::ReadInputReports> fidl_buffer;

    reader.buffer(fidl_buffer.view())->ReadInputReports().ThenExactlyOnce(this);
  }

  void
  OnResult(fidl::WireUnownedResult<FIR::InputReportsReader::ReadInputReports> & result) override;
};

//
//
//
struct surface_platform
{
  //
  //
  //
  surface_input_pfn_t input_pfn;
  void *              data;

  //
  //
  //
  async::Loop loop;

  //
  //
  //
  std::vector<std::unique_ptr<reader_ctx>> ctxs;

  //
  //
  //
  VkExtent2D extent;

  struct
  {
    union surface_buttons pressed;

    struct
    {
      int64_t x;
      int64_t y;
    } absolute;
  } pointer;

  struct
  {
    uint32_t pressed[FIR::wire::kKeyboardMaxPressedKeys + 1];  // sentinel at end
  } keyboard;

  struct
  {
    union surface_buttons pressed;
    uint32_t              contact_count;
  } touch;

  //
  //
  //
  surface_platform();

  void
  input(surface_input_pfn_t input_pfn, void * data);

#if !defined(SPN_VK_SURFACE_FUCHSIA_DISABLE_TRACE) && !defined(NTRACE)
  //
  // Fuchsia tracing
  //
  async::Loop                  trace_loop;
  trace::TraceProviderWithFdio trace_provider;
#endif
};

//
//
//
void
surface_platform::input(surface_input_pfn_t input_pfn, void * data)
{
  this->input_pfn = input_pfn;
  this->data      = data;
}

//
//
//
surface_platform::surface_platform()
    : loop(&kAsyncLoopConfigAttachToCurrentThread)

#if !defined(SPN_VK_SURFACE_FUCHSIA_DISABLE_TRACE) && !defined(NTRACE)
      ,
      trace_loop(&kAsyncLoopConfigNoAttachToCurrentThread),
      trace_provider(trace_loop.dispatcher())
#endif

{
  // probe a range of fd names
  std::string ir_dir = "/dev/class/input-report";

  for (const auto & ir_path : std::filesystem::directory_iterator(ir_dir))
    {
      // try to open path to input-report device
      zx::result input_client_end = component::Connect<FIR::InputDevice>(ir_path.path().c_str());

      if (input_client_end.is_error())
        {
          continue;
        }
#ifndef NDEBUG
      else
        {
          fprintf(stderr, "%s : %s : opened `%s`\n", __FILE__, __func__, ir_path.path().c_str());
        }
#endif

      // create input device
      auto input_device = fidl::WireSyncClient<FIR::InputDevice>(std::move(*input_client_end));

      // create input reports reader
      zx::result input_endpoints = fidl::CreateEndpoints<FIR::InputReportsReader>();

      if (input_endpoints.is_error())
        {
          exit(EXIT_FAILURE);  // Should never happen -- exit with failure.
        }

      auto & [reports_client, reports_server] = input_endpoints.value();

      auto reader_result = input_device->GetInputReportsReader(std::move(reports_server));

      if (reader_result.ok())
        {
          auto reports_client_end =
            fidl::ClientEnd<FIR::InputReportsReader>(std::move(reports_client));

          fidl::WireClient<FIR::InputReportsReader> input_reader(std::move(reports_client_end),
                                                                 loop.dispatcher());

          if (input_reader.is_valid())
            {
              ctxs.emplace_back(new reader_ctx(this,  // Use ctxs.size() as a unique id
                                               static_cast<uint32_t>(ctxs.size()),
                                               input_device,
                                               input_reader));
            }
        }
    }

#if !defined(SPN_VK_SURFACE_FUCHSIA_DISABLE_TRACE) && !defined(NTRACE)
  //
  // Start tracing loop
  //
  // trace.loop = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread)
  zx_status_t const status = trace_loop.StartThread();

  if (status != ZX_OK)
    {
      exit(EXIT_FAILURE);
    }
#endif
}

//
//
//
static void
destroy(struct surface * surface)
{
  struct surface_platform * const platform = surface->platform;

  platform->loop.Shutdown();  // drain input reports

  surface_detach(surface);

  vkDestroySurfaceKHR(surface->vk.i, surface->vk.surface, surface->vk.ac);

  delete platform;

  delete surface;
}

//
//
//
static void
input_keyboard(struct surface_platform *              platform,
               uint32_t                               device_id,
               zx_time_t const                        time,
               FIR::wire::KeyboardInputReport const & report)
{
  if (report.has_pressed_keys3())
    {
      struct surface_event event = {
        .device_id = device_id,
        .timestamp = static_cast<uint64_t>(time),
      };

      //
      // local uninitialized copy of current pressed keys
      //
      // NOTE: sentinel at end
      //
      uint32_t curr[FIR::wire::kKeyboardMaxPressedKeys + 1];

      uint32_t curr_idx = 0;

      for (auto const & fuchsia_key : report.pressed_keys3())
        {
          uint32_t const key = static_cast<uint32_t>(fuchsia_key);
          uint32_t const hid = surface_fuchsia_key_to_hid(key);

          if (hid > 0)
            {
              curr[curr_idx++] = hid;
            }
        }

      // set sentinel
      curr[curr_idx] = 0;

      uint32_t *       prev_st = platform->keyboard.pressed;
      uint32_t const * prev_ld = prev_st;
      uint32_t const * curr_ld = curr;

      //
      // prev and curr keys arrays are in chronological order
      //
      uint32_t curr_key = *curr_ld++;
      uint32_t prev_key = *prev_ld++;

      while (true)
        {
          // is this the curr sentinel?
          if (curr_key == 0)
            {
              // release remaining prev keys
              if (prev_key != 0)
                {
                  event.type = SURFACE_EVENT_TYPE_KEYBOARD_RELEASE;

                  do
                    {
                      event.keyboard.code = prev_key;

                      platform->input_pfn(platform->data, &event);

                      prev_key = *prev_ld++;
                  } while (prev_key != 0);
                }
              break;
            }

          // is this the prev sentinel?
          if (prev_key == 0)
            {
              // press remaining curr keys
              event.type = SURFACE_EVENT_TYPE_KEYBOARD_PRESS;

              do
                {
                  *prev_st++ = curr_key;  // update prev[]

                  event.keyboard.code = curr_key;

                  platform->input_pfn(platform->data, &event);

                  curr_key = *curr_ld++;
              } while (curr_key != 0);

              break;
            }

          // is key still down?
          if (curr_key == prev_key)
            {
              *prev_st++ = curr_key;  // update prev[]

              curr_key = *curr_ld++;
              prev_key = *prev_ld++;
            }
          else  // key was released!
            {
              event.type          = SURFACE_EVENT_TYPE_KEYBOARD_RELEASE;
              event.keyboard.code = prev_key,

              platform->input_pfn(platform->data, &event);

              prev_key = *prev_ld++;
            }
        }

      *prev_st = 0;
    }
}

//
//
//
static void
input_buttons_changed(struct surface_platform *     platform,
                      struct surface_event * const  event,
                      union surface_buttons * const buttons,
                      uint32_t                      changes)
{
  uint32_t ffs;

  while ((ffs = __builtin_ffs(changes)) != 0)
    {
      uint32_t const mask = 1u << (ffs - 1);

      buttons->dword = mask;

      changes ^= mask;

      platform->input_pfn(platform->data, event);
    }
}

//
//
//
static void
input_mouse(struct surface_platform *           platform,
            uint32_t                            device_id,
            zx_time_t const                     time,
            FIR::wire::MouseInputReport const & report)
{
  //
  // The order of mouse events is:
  //
  // 1. released buttons
  // 2. pressed buttons
  // 3. scroll vertical with accumulated buttons
  // 4. scroll horizontal with accumulated buttons
  // 5. movement with accumulated buttons
  //
  if (report.has_movement_x())
    {
      platform->pointer.absolute.x += report.movement_x();

      if (platform->pointer.absolute.x < 0)
        {
          platform->pointer.absolute.x = 0;
        }
      else if (platform->pointer.absolute.x >= platform->extent.width)
        {
          platform->pointer.absolute.x = platform->extent.width - 1;
        }
    }

  if (report.has_movement_y())
    {
      platform->pointer.absolute.y += report.movement_y();

      if (platform->pointer.absolute.y < 0)
        {
          platform->pointer.absolute.y = 0;
        }
      else if (platform->pointer.absolute.y >= platform->extent.height)
        {
          platform->pointer.absolute.y = platform->extent.height - 1;
        }
    }

  //
  // notify of button changes
  //
  uint32_t const prev = platform->pointer.pressed.dword;

  if (report.has_pressed_buttons())
    {
      platform->pointer.pressed.dword = 0;

      for (uint8_t const button : report.pressed_buttons())
        {
          // guaranteed to be <= MOUSE_MAX_NUM_BUTTONS
          platform->pointer.pressed.dword |= (1u << (button - 1));
        }
    }

  uint32_t const curr = platform->pointer.pressed.dword;

  struct surface_event const event_common = {

    .type      = SURFACE_EVENT_TYPE_POINTER_INPUT,
    .device_id = device_id,
    .timestamp = static_cast<uint64_t>(time),
    .pointer   = { .extent  = platform->extent,
                   .buttons = { .dword = curr },
                   .x       = platform->pointer.absolute.x,
                   .y       = platform->pointer.absolute.y },
  };

  //
  // release events are first
  //
  {
    uint32_t released = prev & ~curr;

    if (released != 0)
      {
        struct surface_event event = event_common;

        event.type = SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_RELEASE;

        input_buttons_changed(platform, &event, &event.pointer.buttons, released);
      }
  }

  //
  // press events are second
  //
  {
    uint32_t pressed = ~prev & curr;

    if (pressed != 0)
      {
        struct surface_event event = event_common;

        event.type = SURFACE_EVENT_TYPE_POINTER_INPUT_BUTTON_PRESS;

        input_buttons_changed(platform, &event, &event.pointer.buttons, pressed);
      }
  }

  //
  // scroll vertical?
  //
  if (report.has_scroll_v())
    {
      int64_t const scroll_v = report.scroll_v();

      if (scroll_v != 0)
        {
          struct surface_event event = event_common;

          event.type      = SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_V;
          event.pointer.v = scroll_v;

          platform->input_pfn(platform->data, &event);
        };
    }

  //
  // scroll horizontal?
  //
  if (report.has_scroll_h())
    {
      int64_t const scroll_h = report.scroll_h();

      if (scroll_h != 0)
        {
          struct surface_event event = event_common;

          event.type      = SURFACE_EVENT_TYPE_POINTER_INPUT_SCROLL_H;
          event.pointer.h = scroll_h;

          platform->input_pfn(platform->data, &event);
        }
    }

  //
  // finally end with a regular input event
  //
  platform->input_pfn(platform->data, &event_common);
}

//
//
//
static void
input_touch(struct surface_platform *           platform,
            uint32_t                            device_id,
            FIR::wire::TouchDescriptor const &  descriptor,
            zx_time_t const                     time,
            FIR::wire::TouchInputReport const & report)
{
  //
  // The order of touch events is:
  //
  // 1. new contacts with contact count change
  // 2. new contacts with released buttons
  // 3. new contacts with pressed buttons
  // 4. new contacts with accumulated buttons
  //
  struct surface_event event = {
    .device_id = device_id,
    .timestamp = static_cast<uint64_t>(time),
    .touch     = { .extent = platform->extent },
  };

  //
  // report the range of the contact
  //
  FIR::wire::ContactInputDescriptor const & contact_descriptor_0 = descriptor.input().contacts()[0];

  if (contact_descriptor_0.has_position_x())
    {
      FIR::wire::Axis const & axis = contact_descriptor_0.position_x();

      event.touch.contact_axes.x.min = axis.range.min;
      event.touch.contact_axes.x.max = axis.range.max;
    }

  if (contact_descriptor_0.has_position_y())
    {
      FIR::wire::Axis const & axis = contact_descriptor_0.position_y();

      event.touch.contact_axes.y.min = axis.range.min;
      event.touch.contact_axes.y.max = axis.range.max;
    }

  //
  // update contacts
  //
  {
    event.touch.contact_count.prev = platform->touch.contact_count;

    if (report.has_contacts())
      {
        platform->touch.contact_count = 0;

        struct surface_contact * ec = event.touch.contacts;

        for (auto const & rc : report.contacts())
          {
            // guaranteed to be <= TOUCH_MAX_CONTACTS
            platform->touch.contact_count++;

            if (rc.has_position_x())
              {
                ec->x = rc.position_x();
              }
            if (rc.has_position_y())
              {
                ec->y = rc.position_y();
              }
            if (rc.has_pressure())
              {
                ec->pressure = rc.pressure();
              }
            if (rc.has_contact_width())
              {
                ec->width = rc.contact_width();
              }
            if (rc.has_contact_height())
              {
                ec->height = rc.contact_height();
              }

            ec++;
          }
      }

    event.touch.contact_count.curr = platform->touch.contact_count;

    if (event.touch.contact_count.curr != event.touch.contact_count.prev)
      {
        event.type = SURFACE_EVENT_TYPE_TOUCH_INPUT_CONTACT_COUNT;

        platform->input_pfn(platform->data, &event);
      }
  }

  //
  // update buttons
  //
  {
    uint32_t const prev = platform->touch.pressed.dword;

    if (report.has_pressed_buttons())
      {
        platform->touch.pressed.dword = 0;

        for (uint8_t const button : report.pressed_buttons())
          {
            // guaranteed to be <= TOUCH_MAX_NUM_BUTTONS
            platform->touch.pressed.dword |= (1u << (button - 1));
          }
      }

    uint32_t const curr = platform->touch.pressed.dword;

    //
    // release events are first
    //
    {
      uint32_t released = prev & ~curr;

      if (released != 0)
        {
          event.type = SURFACE_EVENT_TYPE_TOUCH_INPUT_BUTTON_RELEASE;

          input_buttons_changed(platform, &event, &event.touch.buttons, released);
        }
    }

    //
    // press events are second
    //
    {
      uint32_t pressed = ~prev & curr;

      if (pressed != 0)
        {
          event.type = SURFACE_EVENT_TYPE_TOUCH_INPUT_BUTTON_PRESS;

          input_buttons_changed(platform, &event, &event.touch.buttons, pressed);
        }
    }
  }

  //
  // end with an input event
  //
  event.type                = SURFACE_EVENT_TYPE_TOUCH_INPUT;
  event.touch.buttons.dword = platform->touch.pressed.dword;

  platform->input_pfn(platform->data, &event);
}

//
//
//
static void
input_consumer_control(struct surface_platform *                     platform,
                       uint32_t                                      device_id,
                       zx_time_t const                               time,
                       FIR::wire::ConsumerControlInputReport const & report)
{
#ifndef NDEBUG
  fprintf(stderr, "%s - FIR::ConsumerControlInputReport\n", __func__);
#endif
}

//
//
//
void
reader_ctx::OnResult(fidl::WireUnownedResult<FIR::InputReportsReader::ReadInputReports> & result)
{
  //
  // NOTE(allanmac): When will this occur?
  //
  if (!result.ok())
    return;

  //
  // Initiate another async read
  //
  fidl::AsyncClientBuffer<FIR::InputReportsReader::ReadInputReports> fidl_buffer;

  reader.buffer(fidl_buffer.view())->ReadInputReports().ThenExactlyOnce(this);

  //
  // Get the reports vector view
  //
  auto const & reports = result->value()->reports;

  for (auto const & report : reports)
    {
      if (report.has_mouse())
        {
          input_mouse(platform,  //
                      device_id,
                      report.event_time(),
                      report.mouse());
        }
      if (report.has_keyboard())
        {
          input_keyboard(platform,  //
                         device_id,
                         report.event_time(),
                         report.keyboard());
        }
      if (report.has_touch())
        {
          input_touch(platform,  //
                      device_id,
                      descriptor->descriptor.touch(),
                      report.event_time(),
                      report.touch());
        }
      if (report.has_consumer_control())
        {
          input_consumer_control(platform,  //
                                 device_id,
                                 report.event_time(),
                                 report.consumer_control());
        }
      //
      // NOTE(allanmac): Not handling SensorInputReports
      //
    }
}

//
//
//
static void
input(struct surface * surface, surface_input_pfn_t input_pfn, void * data)
{
  struct surface_platform * const platform = surface->platform;

  //
  // update input callback and payload
  //
  platform->input(input_pfn, data);

  //
  // drain input reports
  //
  platform->loop.RunUntilIdle();
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
surface_fuchsia_create(VkInstance vk_i, VkAllocationCallbacks const * vk_ac)
{
  //
  // surface
  //
  struct surface * const surface = new struct surface();

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
  surface->platform = new surface_platform();

  //
  // Fuchsia surface
  //
  VkImagePipeSurfaceCreateInfoFUCHSIA const ipsci_fuchsia = {
    .sType           = VK_STRUCTURE_TYPE_IMAGEPIPE_SURFACE_CREATE_INFO_FUCHSIA,
    .pNext           = NULL,
    .flags           = 0,
    .imagePipeHandle = 0
  };

  vk(CreateImagePipeSurfaceFUCHSIA(vk_i, &ipsci_fuchsia, vk_ac, &surface->vk.surface));

  return surface;
}

//
//
//
