// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FPS_PRINTER_H
#define FPS_PRINTER_H

#include <chrono>

#include "magma_util/macros.h"

namespace magma {

class FpsPrinter {
public:
    void OnNewFrame()
    {
        if (!started_) {
            started_ = true;
            t0_ = std::chrono::high_resolution_clock::now();
            return;
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = t1 - t0_;
        if (elapsed.count() > kSlowFrameMs) {
            magma::log(magma::LOG_INFO,
                       "Extra slow frame detected (> %u ms), restarting fps measurement",
                       kSlowFrameMs);
            t0_ = t1;
            return;
        }
        total_ms_ += elapsed.count();
        t0_ = t1;

        if (elapsed_frames_ && (elapsed_frames_ % num_frames_) == 0) {
            float fps = num_frames_ / (total_ms_ / kMsPerSec);
            magma::log(magma::LOG_INFO,
                       "Framerate average for last %u frames: %.2f frames per second", num_frames_,
                       fps);
            total_ms_ = 0;
            // attempt to log once per second
            num_frames_ = fps < 1 ? 1 : fps;
            elapsed_frames_ = 0;
        }

        elapsed_frames_++;
    }

private:
    static constexpr float kMsPerSec = std::chrono::milliseconds(std::chrono::seconds(1)).count();
    static constexpr uint32_t kSlowFrameMs = 2000;

    bool started_ = false;
    uint32_t num_frames_ = 60;
    uint32_t elapsed_frames_ = 0;
    float total_ms_ = 0;
    std::chrono::high_resolution_clock::time_point t0_;
};

} // namespace magma

#endif // FPS_PRINTER_H
