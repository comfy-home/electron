// Copyright (c) 2014 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/ui/views/frameless_view.h"

#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/inspectable_web_contents_view.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#if BUILDFLAG(IS_LINUX)
#include "shell/browser/linux/x11_util.h"
#endif
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace electron {

namespace {

const int kResizeAreaCornerSize = 16;

}  // namespace

FramelessView::FramelessView(NativeWindowViews* window, views::Widget* frame)
    : window_{window}, frame_{frame} {}

FramelessView::~FramelessView() = default;

gfx::Insets FramelessView::RestoredFrameBorderInsets() const {
  return gfx::Insets();
}

int FramelessView::ResizingBorderHitTest(const gfx::Point& point) {
#if BUILDFLAG(IS_LINUX)
  // On Wayland, setBounds() cannot change window position — the compositor
  // controls it. Native xdg_toplevel_resize() is the only way to handle corner
  // resizes that move the window. Use a custom hit-test with corner-radius-
  // aware circles at each corner so resize handles match the rounded
  // corner region, while keeping edge borders thin.
  if (x11_util::IsWayland()) {
    bool can_ever_resize = frame_->widget_delegate()
                               ? frame_->widget_delegate()->CanResize()
                               : false;
    if (!can_ever_resize)
      return HTNOWHERE;
    if (frame_->IsMaximized() || frame_->IsFullscreen())
      return HTNOWHERE;

    int corner = window_->corner_radius();
    if (corner < 0)
      corner = kResizeAreaCornerSize;

    // Corner hit areas are circles inscribed in the corner square of size
    // hit_r, centered at (hit_r, hit_r) from the window corner. This places
    // the circle tangent to the window edges so it covers the visible part
    // of the rounded corner (inside the compositor's input region).
    int hit_r = corner > 40 ? corner * 2 / 5 : corner * 2 / 3;

    // The MD shadow has a downward y offset, making the compositor's input
    // region clip the bottom edge more than the top. Offset bottom corner
    // circles upward so they stay within the input region.
    const int bottom_offset = 4;

    const int edge = 4;
    const int w = width();
    const int h = height();
    const int r2 = hit_r * hit_r;
    const int c2 = corner * corner;

    // Only trigger resize when the cursor is inside the rendered (opaque)
    // content. The visual corner arc is centered at (corner, corner) from
    // each window corner with radius |corner|. Points outside the arc are
    // in the transparent corner area and should not engage resize.
    auto in_opaque = [&](int ax, int ay) {
      int dx = point.x() - ax;
      int dy = point.y() - ay;
      return dx * dx + dy * dy <= c2;
    };

    // Corners: circles of radius hit_r inscribed at each corner,
    // restricted to the opaque region inside the corner arc.
    int cx, cy;
    int dx, dy, dist2;

    cx = hit_r;
    cy = hit_r;
    dx = point.x() - cx;
    dy = point.y() - cy;
    dist2 = dx * dx + dy * dy;
    if (dist2 <= r2 && in_opaque(corner, corner)) {
      return HTTOPLEFT;
    }
    cx = w - hit_r;
    cy = hit_r;
    dx = point.x() - cx;
    dy = point.y() - cy;
    dist2 = dx * dx + dy * dy;
    if (dist2 <= r2 && in_opaque(w - corner, corner)) {
      return HTTOPRIGHT;
    }
    cx = hit_r;
    cy = h - hit_r - bottom_offset;
    dx = point.x() - cx;
    dy = point.y() - cy;
    dist2 = dx * dx + dy * dy;
    if (dist2 <= r2 && in_opaque(corner, h - corner)) {
      return HTBOTTOMLEFT;
    }
    cx = w - hit_r;
    cy = h - hit_r - bottom_offset;
    dx = point.x() - cx;
    dy = point.y() - cy;
    dist2 = dx * dx + dy * dy;
    if (dist2 <= r2 && in_opaque(w - corner, h - corner)) {
      return HTBOTTOMRIGHT;
    }

    // Edges: thin strips between corners
    if (point.y() < edge) {
      return HTTOP;
    }
    if (point.y() >= h - edge) {
      return HTBOTTOM;
    }
    if (point.x() < edge) {
      return HTLEFT;
    }
    if (point.x() >= w - edge) {
      return HTRIGHT;
    }

    return HTNOWHERE;
  }
#endif
  return ResizingBorderHitTestImpl(point, gfx::Insets(kResizeInsideBoundsSize));
}

int FramelessView::ResizingBorderHitTestImpl(const gfx::Point& point,
                                             const gfx::Insets& resize_border) {
  // to be used for resize handles.
  bool can_ever_resize = frame_->widget_delegate()
                             ? frame_->widget_delegate()->CanResize()
                             : false;

  // https://github.com/electron/electron/issues/611
  // If window isn't resizable, we should always return HTNOWHERE, otherwise the
  // hover state of DOM will not be cleared probably.
  if (!can_ever_resize)
    return HTNOWHERE;

  // Don't allow overlapping resize handles when the window is maximized or
  // fullscreen, as it can't be resized in those states.
  bool allow_overlapping_handles =
      !frame_->IsMaximized() && !frame_->IsFullscreen();
  return GetHTComponentForFrame(
      point, allow_overlapping_handles ? resize_border : gfx::Insets(),
      kResizeAreaCornerSize, kResizeAreaCornerSize, can_ever_resize);
}

gfx::Rect FramelessView::GetBoundsForClientView() const {
  return bounds();
}

gfx::Rect FramelessView::GetWindowBoundsForClientBounds(
    const gfx::Rect& client_bounds) const {
  gfx::Rect window_bounds = client_bounds;
  // Enforce minimum size (1, 1) in case that client_bounds is passed with
  // empty size. This could occur when the frameless window is being
  // initialized.
  if (window_bounds.IsEmpty()) {
    window_bounds.set_width(1);
    window_bounds.set_height(1);
  }
  return window_bounds;
}

int FramelessView::NonClientHitTest(const gfx::Point& point) {
  if (frame_->IsFullscreen())
    return HTCLIENT;

  int contents_hit_test = window_->NonClientHitTest(point);
  if (contents_hit_test != HTNOWHERE)
    return contents_hit_test;

  // Support resizing frameless window by dragging the border.
  int frame_component = ResizingBorderHitTest(point);
  if (frame_component != HTNOWHERE)
    return frame_component;

  return HTCLIENT;
}

views::View* FramelessView::TargetForRect(views::View* root,
                                          const gfx::Rect& rect) {
  CHECK_EQ(root, this);

  if (NonClientHitTest(rect.origin()) != HTCLIENT)
    return this;

  return FrameView::TargetForRect(root, rect);
}

gfx::Size FramelessView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return frame_->non_client_view()
      ->GetWindowBoundsForClientBounds(gfx::Rect(
          frame_->client_view()->CalculatePreferredSize(available_size)))
      .size();
}

gfx::Size FramelessView::GetMinimumSize() const {
  if (!window_)
    return gfx::Size();
  return window_->GetMinimumSize();
}

gfx::Size FramelessView::GetMaximumSize() const {
  if (!window_)
    return gfx::Size();
  return window_->GetMaximumSize();
}

BEGIN_METADATA(FramelessView)
END_METADATA

}  // namespace electron
