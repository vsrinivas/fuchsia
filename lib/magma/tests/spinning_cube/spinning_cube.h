// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_SPINNING_CUBE_SPINNING_CUBE_H_
#define EXAMPLES_SPINNING_CUBE_SPINNING_CUBE_H_

#include <chrono>
#include <glm/glm.hpp>
#include <stdint.h>

class SpinningCube {
public:
    SpinningCube();
    ~SpinningCube();

    void Init();
    void set_direction(int direction) { direction_ = direction; }
    void set_color(float r, float g, float b)
    {
        color_[0] = r;
        color_[1] = g;
        color_[2] = b;
    }
    void set_size(uint32_t width, uint32_t height)
    {
        width_ = width;
        height_ = height;
    }
    void SetFlingMultiplier(float drag_distance, float drag_time);
    void UpdateForTimeDelta(float delta_time);
    void UpdateForDragDistance(float distance);
    void UpdateForDragVector(glm::vec2 start_pixel,
                             std::chrono::high_resolution_clock::time_point start_time,
                             glm::vec2 end_pixel,
                             std::chrono::high_resolution_clock::time_point end_time);
    void Draw();

    void OnGLContextLost();

private:
    class GLState;

    void Update();

    bool initialized_;
    uint32_t width_;
    uint32_t height_;

    GLState* state_;
    float fling_multiplier_;
    int direction_;
    float color_[3];

    glm::vec3 axis_;
    float angular_velocity_ = .5;
    glm::mat4 orientation_;
};

#endif // EXAMPLES_SPINNING_CUBE_SPINNING_CUBE_H_
