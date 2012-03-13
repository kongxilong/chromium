// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/background.h"

#include "base/logging.h"
#include "skia/ext/skia_utils_win.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/painter.h"
#include "ui/views/view.h"

namespace views {

// SolidBackground is a trivial Background implementation that fills the
// background in a solid color.
class SolidBackground : public Background {
 public:
  explicit SolidBackground(const SkColor& color) {
    SetNativeControlColor(color);
  }

  void Paint(gfx::Canvas* canvas, View* view) const {
    // Fill the background. Note that we don't constrain to the bounds as
    // canvas is already clipped for us.
    canvas->sk_canvas()->drawColor(get_color());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SolidBackground);
};

class BackgroundPainter : public Background {
 public:
  BackgroundPainter(bool owns_painter, Painter* painter)
      : owns_painter_(owns_painter), painter_(painter) {
    DCHECK(painter);
  }

  virtual ~BackgroundPainter() {
    if (owns_painter_)
      delete painter_;
  }


  void Paint(gfx::Canvas* canvas, View* view) const {
    Painter::PaintPainterAt(canvas, painter_, view->GetLocalBounds());
  }

 private:
  bool owns_painter_;
  Painter* painter_;

  DISALLOW_COPY_AND_ASSIGN(BackgroundPainter);
};

Background::Background()
    : color_(SK_ColorWHITE)
#if defined(OS_WIN)
    , native_control_brush_(NULL)
#endif
{
}

Background::~Background() {
#if defined(OS_WIN)
  DeleteObject(native_control_brush_);
#endif
}

void Background::SetNativeControlColor(SkColor color) {
  color_ = color;
#if defined(OS_WIN)
  DeleteObject(native_control_brush_);
  native_control_brush_ = NULL;
#endif
}

#if defined(OS_WIN)
HBRUSH Background::GetNativeControlBrush() const {
  if (!native_control_brush_)
    native_control_brush_ = CreateSolidBrush(skia::SkColorToCOLORREF(color_));
  return native_control_brush_;
}
#endif

//static
Background* Background::CreateSolidBackground(const SkColor& color) {
  return new SolidBackground(color);
}

//static
Background* Background::CreateStandardPanelBackground() {
  // TODO(beng): Should be in NativeTheme.
#if defined(USE_AURA)
  return CreateSolidBackground(SK_ColorWHITE);
#else
  return CreateVerticalGradientBackground(SkColorSetRGB(246, 250, 255),
                                          SkColorSetRGB(219, 235, 255));
#endif
}

//static
Background* Background::CreateVerticalGradientBackground(
    const SkColor& color1, const SkColor& color2) {
  Background* background = CreateBackgroundPainter(
      true, Painter::CreateVerticalGradient(color1, color2));
  background->SetNativeControlColor(
      color_utils::AlphaBlend(color1, color2, 128));

  return background;
}

//static
Background* Background::CreateBackgroundPainter(bool owns_painter,
                                                Painter* painter) {
  return new BackgroundPainter(owns_painter, painter);
}

}  // namespace views
