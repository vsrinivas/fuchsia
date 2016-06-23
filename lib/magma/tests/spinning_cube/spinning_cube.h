// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_SPINNING_CUBE_SPINNING_CUBE_H_
#define EXAMPLES_SPINNING_CUBE_SPINNING_CUBE_H_

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
};

#endif // EXAMPLES_SPINNING_CUBE_SPINNING_CUBE_H_
