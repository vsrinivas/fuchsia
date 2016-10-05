// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This example program is based on Simple_VertexShader.c from:

//
// Book:      OpenGL(R) ES 2.0 Programming Guide
// Authors:   Aaftab Munshi, Dan Ginsburg, Dave Shreiner
// ISBN-10:   0321502795
// ISBN-13:   9780321502797
// Publisher: Addison-Wesley Professional
// URLs:      http://safari.informit.com/9780321563835
//            http://www.opengles-book.com
//

#include "spinning_cube.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/vector_angle.hpp>
#include <iostream>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <string>

namespace {

const int kNumVertices = 24;

int GenerateCube(GLuint* vbo_vertices, GLuint* vbo_indices)
{
    const int num_indices = 36;

    const GLfloat cube_vertices[kNumVertices * 3] = {
        // -Y side.
        -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f,

        // +Y side.
        -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f,

        // -Z side.
        -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f, -0.5f,

        // +Z side.
        -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f, 0.5f,

        // -X side.
        -0.5f, -0.5f, -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f, -0.5f,

        // +X side.
        0.5f, -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, -0.5f,
    };

    const GLfloat vertex_normals[kNumVertices * 3] = {
        // -Y side.
        0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f,

        // +Y side.
        0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,

        // -Z side.
        0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f,

        // +Z side.
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f,

        // -X side.
        -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f,

        // +X side.
        1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
    };

    const GLushort cube_indices[] = {// -Y side.
                                     0, 2, 1, 0, 3, 2,

                                     // +Y side.
                                     4, 5, 6, 4, 6, 7,

                                     // -Z side.
                                     8, 9, 10, 8, 10, 11,

                                     // +Z side.
                                     12, 15, 14, 12, 14, 13,

                                     // -X side.
                                     16, 17, 18, 16, 18, 19,

                                     // +X side.
                                     20, 23, 22, 20, 22, 21};

    if (vbo_vertices) {
        glGenBuffers(1, vbo_vertices);
        glBindBuffer(GL_ARRAY_BUFFER, *vbo_vertices);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cube_vertices) + sizeof(vertex_normals), nullptr,
                     GL_STATIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(cube_vertices), cube_vertices);
        glBufferSubData(GL_ARRAY_BUFFER, sizeof(cube_vertices), sizeof(vertex_normals),
                        vertex_normals);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    if (vbo_indices) {
        glGenBuffers(1, vbo_indices);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, *vbo_indices);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(cube_indices), cube_indices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    return num_indices;
}

GLuint LoadShader(GLenum type, const char* shader_source)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shader_source, NULL);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        GLsizei expected_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &expected_length);
        std::string log;
        log.resize(expected_length); // Includes null terminator.
        GLsizei actual_length = 0;
        glGetShaderInfoLog(shader, expected_length, &actual_length, &log[0]);
        log.resize(actual_length); // Excludes null terminator.
        std::cerr << "Compilation of shader failed: " << log;
        glDeleteShader(shader);
        return 0;
    }

    return shader;
}

GLuint LoadProgram(const char* vertex_shader_source, const char* fragment_shader_source)
{
    GLuint vertex_shader = LoadShader(GL_VERTEX_SHADER, vertex_shader_source);
    if (!vertex_shader)
        return 0;

    GLuint fragment_shader = LoadShader(GL_FRAGMENT_SHADER, fragment_shader_source);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    GLuint program_object = glCreateProgram();
    glAttachShader(program_object, vertex_shader);
    glAttachShader(program_object, fragment_shader);
    glLinkProgram(program_object);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = 0;
    glGetProgramiv(program_object, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLsizei expected_length = 0;
        glGetProgramiv(program_object, GL_INFO_LOG_LENGTH, &expected_length);
        std::string log;
        log.resize(expected_length); // Includes null terminator.
        GLsizei actual_length = 0;
        glGetProgramInfoLog(program_object, expected_length, &actual_length, &log[0]);
        log.resize(actual_length); // Excludes null terminator.
        std::cerr << "Linking program failed: " << log;
        glDeleteProgram(program_object);
        return 0;
    }

    return program_object;
}

} // namespace

class SpinningCube::GLState {
public:
    GLState();

    void OnGLContextLost();

    GLfloat angle_; // Survives losing the GL context.

    GLuint program_object_;
    GLint position_location_;
    GLint normal_location_;
    GLint color_location_;
    GLint mvp_location_;
    GLuint vbo_vertices_;
    GLuint vbo_indices_;
    int num_indices_;
    glm::mat4 mvp_matrix_;
};

SpinningCube::GLState::GLState() : angle_(0) { OnGLContextLost(); }

void SpinningCube::GLState::OnGLContextLost()
{
    program_object_ = 0;
    position_location_ = 0;
    normal_location_ = 0;
    color_location_ = 0;
    mvp_location_ = 0;
    vbo_vertices_ = 0;
    vbo_indices_ = 0;
    num_indices_ = 0;
}

SpinningCube::SpinningCube()
    : initialized_(false), width_(0), height_(0), state_(new GLState()), fling_multiplier_(1.0f),
      direction_(1), color_(), axis_(1, 0, 0)
{
    state_->angle_ = 45.0f;
    set_color(1.0, 1.0, 1.0);
}

SpinningCube::~SpinningCube()
{
    if (!initialized_)
        return;
    if (state_->vbo_vertices_)
        glDeleteBuffers(1, &state_->vbo_vertices_);
    if (state_->vbo_indices_)
        glDeleteBuffers(1, &state_->vbo_indices_);
    if (state_->program_object_)
        glDeleteProgram(state_->program_object_);
}

void SpinningCube::Init()
{
    const char vertex_shader_source[] =
        "uniform mat4 u_mvpMatrix;                                       \n"
        "attribute vec4 a_position;                                      \n"
        "attribute vec4 a_normal;                                        \n"
        "uniform vec3 u_color;                                           \n"
        "varying vec4 v_color;                                           \n"
        "void main()                                                     \n"
        "{                                                               \n"
        "   gl_Position = u_mvpMatrix * a_position;                      \n"
        "   vec4 rotated_normal = u_mvpMatrix * a_normal;                \n"
        "   vec4 light_direction = normalize(vec4(0.0, 1.0, -1.0, 0.0)); \n"
        "   float directional_capture =                                  \n"
        "       clamp(dot(rotated_normal, light_direction), 0.0, 1.0);   \n"
        "   float light_intensity = 0.6 * directional_capture + 0.4;     \n"
        "   vec3 base_color = a_position.xyz + 0.5;                      \n"
        "   vec3 color = base_color * u_color;                           \n"
        "   v_color = vec4(color * light_intensity, 1.0);                \n"
        "}                                                               \n";

    const char fragment_shader_source[] = "precision mediump float;                            \n"
                                          "varying vec4 v_color;                               \n"
                                          "void main()                                         \n"
                                          "{                                                   \n"
                                          "   gl_FragColor = v_color;                          \n"
                                          "}                                                   \n";

    state_->program_object_ = LoadProgram(vertex_shader_source, fragment_shader_source);
    state_->position_location_ = glGetAttribLocation(state_->program_object_, "a_position");
    state_->normal_location_ = glGetAttribLocation(state_->program_object_, "a_normal");
    state_->color_location_ = glGetUniformLocation(state_->program_object_, "u_color");
    state_->mvp_location_ = glGetUniformLocation(state_->program_object_, "u_mvpMatrix");
    state_->num_indices_ = GenerateCube(&state_->vbo_vertices_, &state_->vbo_indices_);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glEnable(GL_DEPTH_TEST);
    initialized_ = true;
}

void SpinningCube::OnGLContextLost()
{
    initialized_ = false;
    state_->OnGLContextLost();
}


void SpinningCube::UpdateForTimeDelta(float delta_time)
{
    orientation_ = glm::rotate(glm::mat4(), angular_velocity_ * delta_time, axis_) * orientation_;

    Update();
}

glm::vec2 normalize_to_screen(glm::vec2 pixel, glm::vec2 size)
{
    return (pixel - size / 2.0f) / size.y;
}

glm::vec3 project_to_unit_sphere(glm::vec2 p)
{

    float r = 0.5;

    float z = glm::sqrt((r * r) / (p.x * p.x + p.y * p.y));

    return glm::normalize(glm::vec3(p, z));
}

void SpinningCube::UpdateForDragVector(glm::vec2 start_pixel,
                                       std::chrono::high_resolution_clock::time_point start_time,
                                       glm::vec2 end_pixel,
                                       std::chrono::high_resolution_clock::time_point end_time)
{

    // printf("got pixels (%f, %f) and (%f, %f)\n", start_pixel.x, start_pixel.y, end_pixel.x,
    // end_pixel.y);
    glm::vec2 size(width_, height_);
    if (glm::length(normalize_to_screen(start_pixel, size)) > 0.5 ||
        glm::length(normalize_to_screen(end_pixel, size)) > 0.5)
        return;

    glm::vec3 start = project_to_unit_sphere(normalize_to_screen(start_pixel, size));
    glm::vec3 end = project_to_unit_sphere(normalize_to_screen(end_pixel, size));

    axis_ = glm::cross(start, end);
    if (glm::length(axis_) == 0.0) {
        // start and end are the same, so choose an arbitrary axis
        axis_ = glm::vec3(1, 0, 0);
    }
    axis_ = glm::normalize(axis_);

    // radians
    float theta = glm::angle(start, end);

    float seconds = std::chrono::duration<float>(end_time - start_time).count();

    // radians per second
    angular_velocity_ = theta / seconds;

    orientation_ = glm::rotate(glm::mat4(), theta, axis_) * orientation_;

    Update();
}

void SpinningCube::Draw()
{
    glViewport(0, 0, width_, height_);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(state_->program_object_);
    glBindBuffer(GL_ARRAY_BUFFER, state_->vbo_vertices_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state_->vbo_indices_);
    glVertexAttribPointer(state_->position_location_, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat),
                          0);
    glVertexAttribPointer(state_->normal_location_, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat),
                          reinterpret_cast<void*>(3 * sizeof(GLfloat) * kNumVertices));
    glEnableVertexAttribArray(state_->position_location_);
    glEnableVertexAttribArray(state_->normal_location_);
    glUniformMatrix4fv(state_->mvp_location_, 1, GL_FALSE, glm::value_ptr(state_->mvp_matrix_));
    glUniform3fv(state_->color_location_, 1, color_);
    glDrawElements(GL_TRIANGLES, state_->num_indices_, GL_UNSIGNED_SHORT, 0);
}

void SpinningCube::Update()
{
    float aspect = static_cast<GLfloat>(width_) / static_cast<GLfloat>(height_);

    glm::mat4 perspective = glm::perspective(glm::radians(60.0f), aspect, 1.0f, 20.f);

    glm::mat4 modelview = glm::translate(glm::mat4(), glm::vec3(0.0, 0.0, -2.0)) * orientation_;

    state_->mvp_matrix_ = perspective * modelview;
}
