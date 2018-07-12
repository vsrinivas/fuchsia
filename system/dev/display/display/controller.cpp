// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>
#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <zircon/device/display-controller.h>

#include "controller.h"
#include "client.h"
#include "fuchsia/display/c/fidl.h"

namespace {

void on_displays_changed(void* ctx, uint64_t* displays_added, uint32_t added_count,
                         uint64_t* displays_removed, uint32_t removed_count) {
    static_cast<display::Controller*>(ctx)->OnDisplaysChanged(
            displays_added, added_count, displays_removed, removed_count);
}

void on_display_vsync(void* ctx, uint64_t display, zx_time_t timestamp,
                      void** handles, uint32_t handle_count) {
    static_cast<display::Controller*>(ctx)->OnDisplayVsync(display, timestamp,
                                                           handles, handle_count);
}

display_controller_cb_t dc_cb = {
    .on_displays_changed = on_displays_changed,
    .on_display_vsync = on_display_vsync,
};

} // namespace

namespace display {

void Controller::PopulateDisplayMode(const edid::timing_params_t& params, display_mode_t* mode) {
    mode->pixel_clock_10khz = params.pixel_freq_10khz;
    mode->h_addressable = params.horizontal_addressable;
    mode->h_front_porch = params.horizontal_front_porch;
    mode->h_sync_pulse = params.horizontal_sync_pulse;
    mode->h_blanking = params.horizontal_blanking;
    mode->v_addressable = params.vertical_addressable;
    mode->v_front_porch = params.vertical_front_porch;
    mode->v_sync_pulse = params.vertical_sync_pulse;
    mode->v_blanking = params.vertical_blanking;
    mode->mode_flags = (params.vertical_sync_polarity ? MODE_FLAG_VSYNC_POSITIVE : 0)
            | (params.horizontal_sync_polarity ? MODE_FLAG_HSYNC_POSITIVE : 0);
}

void Controller::OnDisplaysChanged(uint64_t* displays_added, uint32_t added_count,
                                   uint64_t* displays_removed, uint32_t removed_count) {
    uint64_t added_success[added_count];
    int32_t added_success_count = 0;
    uint64_t removed_success[removed_count];
    int32_t removed_success_count = 0;

    fbl::AutoLock lock(&mtx_);
    for (unsigned i = 0; i < removed_count; i++) {
        auto target = displays_.erase(displays_removed[i]);
        if (target) {
            removed_success[removed_success_count++] = displays_removed[i];

            image_node_t* node;
            while ((node = list_remove_head_type(&target->images, image_node_t, link)) != nullptr) {
                node->self->StartRetire();
                node->self->OnRetire();
                node->self.reset();
            }
        } else {
            zxlogf(TRACE, "Unknown display %ld removed\n", displays_removed[i]);
        }
    }

    for (unsigned i = 0; i < added_count; i++) {
        fbl::AllocChecker ac;
        fbl::unique_ptr<DisplayInfo> info = fbl::make_unique_checked<DisplayInfo>(&ac);
        if (!ac.check()) {
            zxlogf(INFO, "Out of memory when processing display hotplug\n");
            break;
        }
        info->pending_layer_change = false;
        info->vsync_layer_count = 0;

        info->id = displays_added[i];
        if (ops_.ops->get_display_info(ops_.ctx, info->id, &info->info) != ZX_OK) {
            zxlogf(TRACE, "Error getting display info for %ld\n", info->id);
            continue;
        }
        if (info->info.edid_present) {
            const char* edid_err = "No preferred timing";
            if (!info->edid.Init(info->info.panel.edid.data,
                                 info->info.panel.edid.length, &edid_err)) {
                zxlogf(TRACE, "Failed to parse edid \"%s\"\n", edid_err);
                continue;
            }

            // Go through all the display mode timings and record whether or not
            // a basic layer configuration is acceptable.
            layer_t test_layer = {};
            layer_t* test_layers[] = { &test_layer };
            test_layer.cfg.primary.image.pixel_format = info->info.pixel_formats[0];

            display_config_t test_config;
            const display_config_t* test_configs[] = { &test_config };
            test_config.display_id = displays_added[i];
            test_config.layer_count = 1;
            test_config.layers = test_layers;

            auto timings = info->edid.begin();
            uint32_t idx = 0;
            bool found_timing = false;
            while (timings != info->edid.end()) {
                uint32_t width = timings->horizontal_addressable;
                uint32_t height = timings->vertical_addressable;

                test_layer.cfg.primary.image.width = width;
                test_layer.cfg.primary.image.height = height;
                test_layer.cfg.primary.src_frame.width = width;
                test_layer.cfg.primary.src_frame.height = height;
                test_layer.cfg.primary.dest_frame.width = width;
                test_layer.cfg.primary.dest_frame.height = height;
                PopulateDisplayMode(*timings, &test_config.mode);

                uint32_t display_cfg_result;
                uint32_t layer_result = 0;
                uint32_t* display_layer_results[] = { &layer_result };
                ops_.ops->check_configuration(ops_.ctx, test_configs, &display_cfg_result,
                                              display_layer_results, 1);
                if (display_cfg_result != CONFIG_DISPLAY_OK) {
                    fbl::AllocChecker ac;
                    info->skipped_edid_timings.push_back(idx, &ac);
                    if (!ac.check()) {
                        zxlogf(WARN, "Edid skip allocation failed\n");
                        found_timing = false;
                        break;
                    }
                } else {
                    found_timing = true;
                }

                idx++;
                ++timings;
            }

            if (!found_timing) {
                zxlogf(INFO, "Display with no compatible edid timings\n");
                continue;
            }

            if (zxlog_level_enabled_etc(DDK_LOG_TRACE)) {
                info->edid.Print([](const char* str) {printf("%s", str);});
            }
        }

        uint64_t id = info->id;
        if (displays_.insert_or_find(fbl::move(info))) {
            added_success[added_success_count++] = id;
        } else {
            zxlogf(INFO, "Ignoring duplicate display\n");
        }
    }

    zx_status_t status;
    if (vc_client_) {
        status = vc_client_->OnDisplaysChanged(added_success, added_success_count,
                                               removed_success, removed_success_count);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error when processing hotplug (%d)\n", status);
        }
    }
    if (primary_client_) {
        status = primary_client_->OnDisplaysChanged(added_success, added_success_count,
                                                    removed_success, removed_success_count);
        if (status != ZX_OK) {
            zxlogf(INFO, "Error when processing hotplug (%d)\n", status);
        }
    }
}

void Controller::OnDisplayVsync(uint64_t display_id, zx_time_t timestamp,
                                void** handles, uint32_t handle_count) {
    fbl::AutoLock lock(&mtx_);
    DisplayInfo* info = nullptr;
    for (auto& display_config : displays_) {
        if (display_config.id == display_id) {
            info = &display_config;
            break;
        }
    }

    if (!info) {
        return;
    }

    // See ::ApplyConfig for more explaination of how vsync image tracking works.
    //
    // If there's a pending layer change, don't process any present/retire actions
    // until the change is complete.
    if (info->pending_layer_change) {
        bool done;
        if (handle_count != info->vsync_layer_count) {
            // There's an unexpected number of layers, so wait until the next vsync.
            done = false;
        } else if (list_is_empty(&info->images)) {
            // If the images list is empty, then we can't have any pending layers and
            // the change is done when there are no handles being displayed.
            ZX_ASSERT(info->vsync_layer_count == 0);
            done = handle_count == 0;
        } else {
            // Otherwise the change is done when the last handle_count==info->layer_count
            // images match the handles in the correct order.
            auto node = list_peek_tail_type(&info->images, image_node_t, link);
            int32_t handle_idx = handle_count - 1;
            while (handle_idx >= 0 && node != nullptr) {
                if (handles[handle_idx] != node->self->info().handle) {
                    break;
                }
                node = list_prev_type(&info->images, &node->link, image_node_t, link);
                handle_idx--;
            }
            done = handle_idx == -1;
        }

        if (done) {
            info->pending_layer_change = false;
            info->switching_client = false;

            if (active_client_ && info->delayed_apply) {
                active_client_->ReapplyConfig();
            }
        }
    }

    // Drop the vsync event if we're in the middle of switching clients, since we don't want to
    // send garbage image ids. Switching clients is rare enough that any minor timing issues that
    // this could cause aren't worth worrying about.
    if (!info->switching_client) {
        uint64_t images[handle_count];
        image_node_t* cur;
        list_for_every_entry(&info->images, cur, image_node_t, link) {
            for (unsigned i = 0; i < handle_count; i++) {
                if (handles[i] == cur->self->info().handle) {
                    images[i] = cur->self->id;
                    break;
                }
            }
        }

        if (vc_applied_ && vc_client_) {
            vc_client_->OnDisplayVsync(display_id, timestamp, images, handle_count);
        } else if (!vc_applied_ && primary_client_) {
            primary_client_->OnDisplayVsync(display_id, timestamp, images, handle_count);
        }
    } else {
        zxlogf(TRACE, "Dropping vsync\n");
    }

    if (info->pending_layer_change) {
        return;
    }

    // Since we know there are no pending layer changes, we know that every layer (i.e z_index)
    // has an image. So every image either matches a handle (in which case it's being displayed),
    // is older than its layer's image (i.e. in front of in the queue) and can be retired, or is
    // newer than its layer's image (i.e. behind in the queue) and has yet to be presented.
    uint32_t z_indices[handle_count];
    for (unsigned i = 0; i < handle_count; i++) {
        z_indices[i] = UINT32_MAX;
    }
    image_node_t* cur;
    image_node_t* tmp;
    list_for_every_entry_safe(&info->images, cur, tmp, image_node_t, link) {
        bool z_already_matched = false;
        for (unsigned i = 0; i < handle_count; i++) {
            if (handles[i] == cur->self->info().handle) {
                z_indices[i] = cur->self->z_index();
                z_already_matched = true;
                break;
            } else if (z_indices[i] == cur->self->z_index()) {
                z_already_matched = true;
                break;
            }
        }

        // Retire any images for which we don't already have a z-match, since
        // those are older than whatever is currently in their layer.
        if (!z_already_matched) {
            list_delete(&cur->link);
            cur->self->OnRetire();
            cur->self.reset();
        }
    }
}

void Controller::ApplyConfig(DisplayConfig* configs[], int32_t count,
                             bool is_vc, uint32_t client_stamp) {
    const display_config_t* display_configs[count];
    uint32_t display_count = 0;
    {
        fbl::AutoLock lock(&mtx_);
        // The fact that there could already be a vsync waiting to be handled when a config
        // is applied means that a vsync with no handle for a layer could be interpreted as either
        // nothing in the layer has been presented or everything in the layer can be retired. To
        // prevent that ambiguity, we don't allow a layer to be disabled until an image from
        // it has been displayed.
        //
        // Since layers can be moved between displays but the implementation only supports
        // tracking the image in one display's queue, we need to ensure that the old display is
        // done with the a migrated image before the new display is done with it. This means
        // that the new display can't flip until the configuration change is done. However, we
        // don't want to completely prohibit flips, as that would add latency if the layer's new
        // image is being waited for when the configuration is applied.
        //
        // To handle both of these cases, we force all layer changes to complete before the client
        // can apply a new configuration. We allow the client to apply a more complete version of
        // the configuration, although Client::HandleApplyConfig won't migrate a layer's current
        // image if there is also a pending image.
        if (vc_applied_ != is_vc || applied_stamp_ != client_stamp) {
            for (int i = 0; i < count; i++) {
                auto* config = configs[i];
                auto display = displays_.find(config->id);
                if (!display.IsValid()) {
                    continue;
                }

                if (display->pending_layer_change) {
                    display->delayed_apply = true;
                    return;
                }
            }
        }

        for (int i = 0; i < count; i++) {
            auto* config = configs[i];
            auto display = displays_.find(config->id);
            if (!display.IsValid()) {
                continue;
            }

            display->switching_client = is_vc != vc_applied_;
            display->pending_layer_change =
                    config->apply_layer_change() || display->switching_client;
            display->vsync_layer_count = config->vsync_layer_count();
            display->delayed_apply = false;

            if (display->vsync_layer_count == 0) {
                continue;
            }

            display_configs[display_count++] = config->current_config();

            for (auto& layer_node : config->get_current_layers()) {
                Layer* layer = layer_node.layer;
                fbl::RefPtr<Image> image = layer->current_image();

                if (layer->is_skipped() || !image) {
                    continue;
                }

                // Set the image z index so vsync knows what layer the image is in
                image->set_z_index(layer->z_order());
                image->StartPresent();

                // It's possible that the image's layer was moved between displays. The logic around
                // pending_layer_change guarantees that the old display will be done with the image
                // before the new display is, so deleting it from the old list is fine.
                //
                // Even if we're on the same display, the entry needs to be moved to the end of the
                // list to ensure that the last config->current.layer_count elements in the queue
                // are the current images.
                if (list_in_list(&image->node.link)) {
                    list_delete(&image->node.link);
                } else {
                    image->node.self = image;
                }
                list_add_tail(&display->images, &image->node.link);
            }
        }

        vc_applied_ = is_vc;
        applied_stamp_ = client_stamp;
    }

    ops_.ops->apply_configuration(ops_.ctx, display_configs, display_count);
}

void Controller::ReleaseImage(Image* image) {
    ops_.ops->release_image(ops_.ctx, &image->info());
}

void Controller::SetVcMode(uint8_t vc_mode) {
    fbl::AutoLock lock(&mtx_);
    vc_mode_ = vc_mode;
    HandleClientOwnershipChanges();
}

void Controller::HandleClientOwnershipChanges() {
    ClientProxy* new_active;
    if (vc_mode_ == fuchsia_display_VirtconMode_FORCED
            || (vc_mode_ == fuchsia_display_VirtconMode_FALLBACK && primary_client_ == nullptr)) {
        new_active = vc_client_;
    } else {
        new_active = primary_client_;
    }

    if (new_active != active_client_) {
        if (active_client_) {
            active_client_->SetOwnership(false);
        }
        if (new_active) {
            new_active->SetOwnership(true);
        }
        active_client_ = new_active;
    }
}

void Controller::OnClientDead(ClientProxy* client) {
    fbl::AutoLock lock(&mtx_);
    if (client == vc_client_) {
        vc_client_ = nullptr;
        vc_mode_ = fuchsia_display_VirtconMode_INACTIVE;
    } else if (client == primary_client_) {
        primary_client_ = nullptr;
    }
    HandleClientOwnershipChanges();
}

bool Controller::GetPanelConfig(uint64_t display_id, const edid::Edid** edid,
                                const fbl::Vector<uint32_t>** skipped_edid_timings,
                                const display_params_t** params) {
    ZX_DEBUG_ASSERT(mtx_trylock(&mtx_) == thrd_busy);
    for (auto& display : displays_) {
        if (display.id == display_id) {
            if (display.info.edid_present) {
                *edid = &display.edid;
                *skipped_edid_timings = &display.skipped_edid_timings;
                *params = nullptr;
            } else {
                *params = &display.info.panel.params;
                *edid = nullptr;
            }
            return true;
        }
    }
    return false;
}

#define GET_DISPLAY_INFO(FN_NAME, COUNT_FIELD, TYPE_FIELD, TYPE) \
bool Controller::FN_NAME(uint64_t display_id, uint32_t* count_out, \
                         fbl::unique_ptr<TYPE[]>* data_out) { \
    ZX_DEBUG_ASSERT(mtx_trylock(&mtx_) == thrd_busy); \
    for (auto& display : displays_) { \
        if (display.id == display_id) { \
            *count_out = display.info.COUNT_FIELD; \
            fbl::AllocChecker ac; \
            *data_out = fbl::unique_ptr<TYPE[]>(new (&ac) TYPE[*count_out]); \
            if (!ac.check()) { \
                return false; \
            } \
            memcpy(data_out->get(), display.info.TYPE_FIELD, sizeof(TYPE) * *count_out); \
            return true; \
        } \
    } \
    return false; \
}

GET_DISPLAY_INFO(GetCursorInfo, cursor_info_count, cursor_infos, cursor_info_t)
GET_DISPLAY_INFO(GetSupportedPixelFormats, pixel_format_count, pixel_formats, zx_pixel_format_t);

zx_status_t Controller::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
    return DdkOpenAt(dev_out, "", flags);
}

zx_status_t Controller::DdkOpenAt(zx_device_t** dev_out, const char* path, uint32_t flags) {
    fbl::AutoLock lock(&mtx_);

    bool is_vc = strcmp("virtcon", path) == 0;
    if ((is_vc && vc_client_) || (!is_vc && primary_client_)) {
        zxlogf(TRACE, "Already bound\n");
        return ZX_ERR_ALREADY_BOUND;
    }

    fbl::AllocChecker ac;
    auto client = fbl::make_unique_checked<ClientProxy>(&ac, this, is_vc);
    if (!ac.check()) {
        zxlogf(TRACE, "Failed to alloc client\n");
        return ZX_ERR_NO_MEMORY;
    }

    zx_status_t status = client->Init();
    if (status != ZX_OK) {
        zxlogf(TRACE, "Failed to init client %d\n", status);
        return status;
    }

    // Add all existing displays to the client
    if (displays_.size() > 0) {
        uint64_t current_displays[displays_.size()];
        int idx = 0;
        for (const DisplayInfo& display : displays_) {
            current_displays[idx++] = display.id;
        }
        if ((status = client->OnDisplaysChanged(current_displays, idx, nullptr, 0)) != ZX_OK) {
            zxlogf(TRACE, "Failed to init client %d\n", status);
            return status;
        }
    }

    if ((status = client->DdkAdd(is_vc ? "dc-vc" : "dc", DEVICE_ADD_INSTANCE)) != ZX_OK) {
        zxlogf(TRACE, "Failed to add client %d\n", status);
        return status;
    }

    ClientProxy* client_ptr = client.release();
    *dev_out = client_ptr->zxdev();

    zxlogf(TRACE, "New client connected at \"%s\"\n", path);

    if (is_vc) {
        vc_client_ = client_ptr;
    } else {
        primary_client_ = client_ptr;
    }
    HandleClientOwnershipChanges();

    return ZX_OK;
}

zx_status_t Controller::Bind(fbl::unique_ptr<display::Controller>* device_ptr) {
    zx_status_t status;
    if (device_get_protocol(parent_, ZX_PROTOCOL_DISPLAY_CONTROLLER_IMPL, &ops_)) {
        ZX_DEBUG_ASSERT_MSG(false, "Display controller bind mismatch");
        return ZX_ERR_NOT_SUPPORTED;
    }

    status = loop_.StartThread("display-client-loop", &loop_thread_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to start loop %d\n", status);
        return status;
    }

    if ((status = DdkAdd("display-controller")) != ZX_OK) {
        zxlogf(ERROR, "Failed to add display core device %d\n", status);
        return status;
    }
    __UNUSED auto ptr = device_ptr->release();

    ops_.ops->set_display_controller_cb(ops_.ctx, this, &dc_cb);

    return ZX_OK;
}

void Controller::DdkUnbind() {
    {
        fbl::AutoLock lock(&mtx_);
        if (vc_client_) {
            vc_client_->Close();
        }
        if (primary_client_) {
            primary_client_->Close();
        }
    }
    DdkRemove();
}

void Controller::DdkRelease() {
    delete this;
}

Controller::Controller(zx_device_t* parent) : ControllerParent(parent) {
    mtx_init(&mtx_, mtx_plain);
}

// ControllerInstance methods

} // namespace display

zx_status_t display_controller_bind(void* ctx, zx_device_t* parent) {
    fbl::AllocChecker ac;
    fbl::unique_ptr<display::Controller> core(new (&ac) display::Controller(parent));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return core->Bind(&core);
}
