// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/base/cocoa/fullscreen_window_manager.h"


#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"
#import "ui/base/test/ui_cocoa_test_helper.h"

typedef ui::CocoaTest FullscreenWindowManagerTest;

TEST_F(FullscreenWindowManagerTest, EnterExit) {
  scoped_nsobject<FullscreenWindowManager> manager(
      [[FullscreenWindowManager alloc]
          initWithWindow:test_window()
           desiredScreen:[NSScreen mainScreen]]);

  SystemUIMode mode = kUIModeNormal;
  GetSystemUIMode(&mode, NULL);
  EXPECT_EQ(kUIModeNormal, mode);

  [manager enterFullscreenMode];
  GetSystemUIMode(&mode, NULL);
  EXPECT_EQ(kUIModeAllHidden, mode);

  [manager exitFullscreenMode];
  GetSystemUIMode(&mode, NULL);
  EXPECT_EQ(kUIModeNormal, mode);
}
