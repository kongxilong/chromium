// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/gtk/extension_infobar_gtk.h"

#include "app/resource_bundle.h"
#include "chrome/browser/extensions/extension_host.h"
#include "chrome/common/extensions/extension.h"
#include "chrome/common/extensions/extension_resource.h"
#include "gfx/gtk_util.h"
#include "grit/theme_resources.h"

ExtensionInfoBarGtk::ExtensionInfoBarGtk(ExtensionInfoBarDelegate* delegate)
    : InfoBar(delegate),
      tracker_(this),
      delegate_(delegate),
      view_(NULL) {
  BuildWidgets();
}

ExtensionInfoBarGtk::~ExtensionInfoBarGtk() {
  // This view is not owned by us, so unparent.
  gtk_widget_unparent(view_->native_view());
}

void ExtensionInfoBarGtk::OnImageLoaded(
    SkBitmap* image, ExtensionResource resource, int index) {
  if (!delegate_)
    return;  // The delegate can go away while we asynchronously load images.

  ResourceBundle& rb = ResourceBundle::GetSharedInstance();

  SkBitmap* icon;
  if (!image || image->empty())
    icon = rb.GetBitmapNamed(IDR_EXTENSIONS_SECTION);
  else
    icon = image;

  // TODO(finnur): We now have the icon for the menu button, show the menu
  // button and layout.
}

void ExtensionInfoBarGtk::BuildWidgets() {
  // Start loading the image for the menu button.
  ExtensionResource icon_resource;
  Extension* extension = delegate_->extension_host()->extension();
  Extension::Icons size =
      extension->GetIconPathAllowLargerSize(&icon_resource,
                                            Extension::EXTENSION_ICON_BITTY);
  if (!icon_resource.relative_path().empty()) {
    // Create a tracker to load the image. It will report back on OnImageLoaded.
    tracker_.LoadImage(extension, icon_resource, gfx::Size(size, size),
                       ImageLoadingTracker::DONT_CACHE);
  } else {
    OnImageLoaded(NULL, icon_resource, 0);  // |image|, |index|.
  }

  ExtensionHost* extension_host = delegate_->extension_host();
  view_ = extension_host->view();
  gtk_box_pack_start(GTK_BOX(hbox_), view_->native_view(), TRUE, TRUE, 0);

  g_signal_connect(view_->native_view(), "size_allocate",
                   G_CALLBACK(&OnSizeAllocateThunk), this);
  gtk_widget_show_all(border_bin_.get());
}

void ExtensionInfoBarGtk::OnSizeAllocate(GtkWidget* widget,
                                         GtkAllocation* allocation) {
  gfx::Size new_size(allocation->width, allocation->height);

  // TODO(finnur): Size the infobar based on HTML content (up to 72 pixels).
}

InfoBar* ExtensionInfoBarDelegate::CreateInfoBar() {
  return new ExtensionInfoBarGtk(this);
}
