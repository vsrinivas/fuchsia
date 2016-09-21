#include "escher/renderer.h"
#include "scenes/app_test_scene.h"
#include "scenes/material_stage.h"

#include <GLFW/glfw3.h>
#include <stdio.h>

static void error_callback(int error, const char *description) {
  fprintf(stderr, "Error: %s\n", description);
}

static void key_callback(GLFWwindow *window, int key, int scancode, int action,
                         int mods) {
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

int main(int argc, char **argv) {
  GLFWwindow *window;

  glfwSetErrorCallback(error_callback);

  if (!glfwInit()) {
    exit(EXIT_FAILURE);
  }

  // TODO(jjosh): there seems to be a bug in the GLFW OS X implementation.
  //              It will give me a 3.2 context, but not if I ask for one
  //              explicitly.  No big deal... this is just a stepping stone to
  //              OpenGL 4.5 on Linux.
  // glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  // glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

  int major, minor, rev;
  glfwGetVersion(&major, &minor, &rev);
  fprintf(stderr, "OpenGL version recieved: %d.%d.%d", major, minor, rev);

  window = glfwCreateWindow(
      1024, 1024, "Escher Waterfall Demo (OpenGL)", NULL, NULL);
  if (!window) {
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  glfwSetKeyCallback(window, key_callback);

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  escher::Stage stage;
  std::unique_ptr<escher::Renderer> renderer;
  AppTestScene scene;
  escher::vec2 focus;

  scene.InitGL();
  InitStageForMaterial(&stage);
  renderer.reset(new escher::Renderer());

  if (!renderer->Init()) {
    glfwTerminate();
    exit(EXIT_FAILURE);
  }

  while (!glfwWindowShouldClose(window)) {
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    focus = escher::vec2(width / 2.0f, height / 2.0f);

    // TODO(jjosh): account for Retina displays & other platforms' equivalents.
    constexpr float kContentScaleFactor = 1.0;
    stage.Resize(escher::SizeI(width, height), kContentScaleFactor,
                 escher::SizeI(0, 0));

    // TODO(abarth): There must be a better way to initialize this information.
    if (!renderer->front_frame_buffer_id()) {
      GLint fbo = 0;
      glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
      renderer->set_front_frame_buffer_id(fbo);
    }

    escher::Model model = scene.GetModel(stage.viewing_volume(), focus);
    model.set_blur_plane_height(12.0f);
    renderer->Render(stage, model);

    glfwSwapBuffers(window);
    glfwPollEvents();
  }

  return 0;
}
