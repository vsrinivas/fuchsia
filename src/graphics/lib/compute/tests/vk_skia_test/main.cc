// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
//
//
#include "include/core/SkCanvas.h"
#include "include/core/SkPath.h"
#include "tests/common/skia/skia_test_app.h"

static void
drawFrameWithSkia(SkCanvas * canvas, uint32_t frame_counter)
{
  const SkScalar scale = 256.0f;
  const SkScalar R     = 0.45f * scale;
  const SkScalar TAU   = 6.2831853f;
  SkPath         path;
  for (int i = 0; i < 5; ++i)
    {
      SkScalar theta = 2 * i * TAU / 5 + (frame_counter / M_PI / 100.);
      if (i == 0)
        {
          path.moveTo(R * cos(theta), R * sin(theta));
        }
      else
        {
          path.lineTo(R * cos(theta), R * sin(theta));
        }
    }
  path.close();
  SkPaint p;
  p.setAntiAlias(true);
  canvas->clear(SK_ColorWHITE);
  canvas->resetMatrix();
  canvas->translate(0.5f * scale, 0.5f * scale);
  canvas->drawPath(path, p);
  canvas->flush();
}

class MyTestApp : public SkiaTestApp {
 public:
  MyTestApp() : SkiaTestApp("vk_skia_test", true, 800, 600)
  {
  }

  virtual void
  drawFrame(SkCanvas * canvas, uint32_t frame_counter)
  {
    drawFrameWithSkia(canvas, frame_counter);
  }
};

int
main(int argc, char const * argv[])
{
  MyTestApp testApp;
  testApp.run();
  return EXIT_SUCCESS;
}
