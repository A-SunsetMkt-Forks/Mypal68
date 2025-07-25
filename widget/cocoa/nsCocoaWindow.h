/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCocoaWindow_h_
#define nsCocoaWindow_h_

#undef DARWIN

#import <Cocoa/Cocoa.h>

#include "mozilla/RefPtr.h"
#include "nsBaseWidget.h"
#include "nsPIWidgetCocoa.h"
#include "nsCocoaUtils.h"
#include "nsTouchBar.h"

class nsCocoaWindow;
class nsChildView;
class nsMenuBarX;
@class ChildView;

typedef struct _nsCocoaWindowList {
  _nsCocoaWindowList() : prev(nullptr), window(nullptr) {}
  struct _nsCocoaWindowList* prev;
  nsCocoaWindow* window;  // Weak
} nsCocoaWindowList;

// NSWindow subclass that is the base class for all of our own window classes.
// Among other things, this class handles the storage of those settings that
// need to be persisted across window destruction and reconstruction, i.e. when
// switching to and from fullscreen mode.
// We don't save shadow, transparency mode or background color because it's not
// worth the hassle - Gecko will reset them anyway as soon as the window is
// resized.
@interface BaseWindow : NSWindow {
  // Data Storage
  NSMutableDictionary* mState;
  BOOL mDrawsIntoWindowFrame;

  // Invalidation disabling
  BOOL mDisabledNeedsDisplay;

  NSTrackingArea* mTrackingArea;

  NSRect mDirtyRect;

  BOOL mBeingShown;
  BOOL mDrawTitle;
  BOOL mBrightTitlebarForeground;
  BOOL mUseMenuStyle;

  nsTouchBar* mTouchBar;
}

- (void)importState:(NSDictionary*)aState;
- (NSMutableDictionary*)exportState;
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState;
- (BOOL)drawsContentsIntoWindowFrame;

// These two methods are like contentRectForFrameRect and frameRectForContentRect,
// but they deal with the rect of the window's "main ChildView" instead of the
// rect of the window's content view. The two are sometimes sized differently: The
// window's content view always covers the entire window, whereas the ChildView
// only covers the full window when drawsContentsIntoWindowFrame is YES. When
// drawsContentsIntoWindowFrame is NO, there's a titlebar-sized gap above the
// ChildView within the content view.
- (NSRect)childViewRectForFrameRect:(NSRect)aFrameRect;
- (NSRect)frameRectForChildViewRect:(NSRect)aChildViewRect;

- (void)mouseEntered:(NSEvent*)aEvent;
- (void)mouseExited:(NSEvent*)aEvent;
- (void)mouseMoved:(NSEvent*)aEvent;
- (void)updateTrackingArea;
- (NSView*)trackingAreaView;

- (void)setBeingShown:(BOOL)aValue;
- (BOOL)isBeingShown;
- (BOOL)isVisibleOrBeingShown;

// Returns an autoreleased NSArray containing the NSViews that we consider the
// "contents" of this window. All views in the returned array are subviews of
// this window's content view. However, the array may not include all of the
// content view's subviews; concretely, the ToolbarWindow implementation will
// exclude its TitlebarGradientView from the array that is returned here.
// In the vast majority of cases, the array will only have a single element:
// this window's mainChildView.
- (NSArray<NSView*>*)contentViewContents;

- (ChildView*)mainChildView;

- (NSArray*)titlebarControls;

- (void)setWantsTitleDrawn:(BOOL)aDrawTitle;
- (BOOL)wantsTitleDrawn;

- (void)setUseBrightTitlebarForeground:(BOOL)aBrightForeground;
- (BOOL)useBrightTitlebarForeground;

- (void)disableSetNeedsDisplay;
- (void)enableSetNeedsDisplay;

- (NSRect)getAndResetNativeDirtyRect;

- (void)setUseMenuStyle:(BOOL)aValue;

- (void)releaseJSObjects;

@end

@interface NSWindow (Undocumented)

// If a window has been explicitly removed from the "window cache" (to
// deactivate it), it's sometimes necessary to "reset" it to reactivate it
// (and put it back in the "window cache").  One way to do this, which Apple
// often uses, is to set the "window number" to '-1' and then back to its
// original value.
- (void)_setWindowNumber:(NSInteger)aNumber;

- (BOOL)bottomCornerRounded;

// Present in the same form on OS X since at least OS X 10.5.
- (NSRect)contentRectForFrameRect:(NSRect)windowFrame styleMask:(NSUInteger)windowStyle;
- (NSRect)frameRectForContentRect:(NSRect)windowContentRect styleMask:(NSUInteger)windowStyle;

// Present since at least OS X 10.5.  The OS calls this method on NSWindow
// (and its subclasses) to find out which NSFrameView subclass to instantiate
// to create its "frame view".
+ (Class)frameViewClassForStyleMask:(NSUInteger)styleMask;

@end

@interface PopupWindow : BaseWindow {
 @private
  BOOL mIsContextMenu;
}

- (id)initWithContentRect:(NSRect)contentRect
                styleMask:(NSUInteger)styleMask
                  backing:(NSBackingStoreType)bufferingType
                    defer:(BOOL)deferCreation;
- (BOOL)isContextMenu;
- (void)setIsContextMenu:(BOOL)flag;
- (BOOL)canBecomeMainWindow;

@end

@interface BorderlessWindow : BaseWindow {
}

- (BOOL)canBecomeKeyWindow;
- (BOOL)canBecomeMainWindow;

@end

@interface WindowDelegate : NSObject <NSWindowDelegate> {
  nsCocoaWindow* mGeckoWindow;  // [WEAK] (we are owned by the window)
  // Used to avoid duplication when we send NS_ACTIVATE and
  // NS_DEACTIVATE to Gecko for toplevel widgets.  Starts out
  // false.
  bool mToplevelActiveState;
  BOOL mHasEverBeenZoomed;
}
+ (void)paintMenubarForWindow:(NSWindow*)aWindow;
- (id)initWithGeckoWindow:(nsCocoaWindow*)geckoWind;
- (void)windowDidResize:(NSNotification*)aNotification;
- (nsCocoaWindow*)geckoWidget;
- (bool)toplevelActiveState;
- (void)sendToplevelActivateEvents;
- (void)sendToplevelDeactivateEvents;
@end

@interface TitlebarGradientView : NSView
@end

// NSWindow subclass for handling windows with toolbars.
@interface ToolbarWindow : BaseWindow {
  // This window's titlebar gradient view, if present.
  // Will be nil if drawsContentsIntoWindowFrame is YES.
  // This view is a subview of the window's content view and gets created and
  // destroyed by updateTitlebarGradientViewPresence.
  TitlebarGradientView* mTitlebarGradientView;  // [STRONG]

  CGFloat mUnifiedToolbarHeight;
  CGFloat mSheetAttachmentPosition;
  NSRect mWindowButtonsRect;
  NSRect mFullScreenButtonRect;
}
- (void)setUnifiedToolbarHeight:(CGFloat)aHeight;
- (CGFloat)unifiedToolbarHeight;
- (CGFloat)titlebarHeight;
- (NSRect)titlebarRect;
- (void)setTitlebarNeedsDisplay;
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState;
- (void)setSheetAttachmentPosition:(CGFloat)aY;
- (CGFloat)sheetAttachmentPosition;
- (void)placeWindowButtons:(NSRect)aRect;
- (void)placeFullScreenButton:(NSRect)aRect;
- (NSPoint)windowButtonsPositionWithDefaultPosition:(NSPoint)aDefaultPosition;
- (NSPoint)fullScreenButtonPositionWithDefaultPosition:(NSPoint)aDefaultPosition;
- (void)windowMainStateChanged;
@end

class nsCocoaWindow final : public nsBaseWidget, public nsPIWidgetCocoa {
 private:
  typedef nsBaseWidget Inherited;

 public:
  nsCocoaWindow();

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSPIWIDGETCOCOA;  // semicolon for clang-format bug 1629756

  [[nodiscard]] virtual nsresult Create(nsIWidget* aParent, nsNativeWidget aNativeParent,
                                        const DesktopIntRect& aRect,
                                        nsWidgetInitData* aInitData = nullptr) override;

  [[nodiscard]] virtual nsresult Create(nsIWidget* aParent, nsNativeWidget aNativeParent,
                                        const LayoutDeviceIntRect& aRect,
                                        nsWidgetInitData* aInitData = nullptr) override;

  virtual void Destroy() override;

  virtual void Show(bool aState) override;
  virtual nsIWidget* GetSheetWindowParent(void) override;
  virtual void Enable(bool aState) override;
  virtual bool IsEnabled() const override;
  virtual void SetModal(bool aState) override;
  virtual void SetFakeModal(bool aState) override;
  virtual bool IsRunningAppModal() override;
  virtual bool IsVisible() const override;
  virtual void SetFocus(Raise) override;
  virtual LayoutDeviceIntPoint WidgetToScreenOffset() override;
  virtual LayoutDeviceIntPoint GetClientOffset() override;
  virtual LayoutDeviceIntSize ClientToWindowSize(const LayoutDeviceIntSize& aClientSize) override;

  virtual void* GetNativeData(uint32_t aDataType) override;

  virtual void ConstrainPosition(bool aAllowSlop, int32_t* aX, int32_t* aY) override;
  virtual void SetSizeConstraints(const SizeConstraints& aConstraints) override;
  virtual void Move(double aX, double aY) override;
  virtual void SetSizeMode(nsSizeMode aMode) override;
  virtual void SuppressAnimation(bool aSuppress) override;
  virtual void HideWindowChrome(bool aShouldHide) override;

  void WillEnterFullScreen(bool aFullScreen);
  void EnteredFullScreen(bool aFullScreen, bool aNativeMode = true);
  virtual bool PrepareForFullscreenTransition(nsISupports** aData) override;
  virtual void PerformFullscreenTransition(FullscreenTransitionStage aStage, uint16_t aDuration,
                                           nsISupports* aData, nsIRunnable* aCallback) override;
  nsresult MakeFullScreen(bool aFullScreen, nsIScreen* aTargetScreen = nullptr) final;
  nsresult MakeFullScreenWithNativeTransition(bool aFullScreen,
                                              nsIScreen* aTargetScreen = nullptr) final;
  NSAnimation* FullscreenTransitionAnimation() const { return mFullscreenTransitionAnimation; }
  void ReleaseFullscreenTransitionAnimation() {
    MOZ_ASSERT(mFullscreenTransitionAnimation, "Should only be called when there is animation");
    [mFullscreenTransitionAnimation release];
    mFullscreenTransitionAnimation = nil;
  }

  virtual void Resize(double aWidth, double aHeight, bool aRepaint) override;
  virtual void Resize(double aX, double aY, double aWidth, double aHeight, bool aRepaint) override;
  NSRect GetClientCocoaRect();
  virtual LayoutDeviceIntRect GetClientBounds() override;
  virtual LayoutDeviceIntRect GetScreenBounds() override;
  void ReportMoveEvent();
  void ReportSizeEvent();
  virtual void SetCursor(nsCursor aDefaultCursor, imgIContainer* aCursorImage, uint32_t aHotspotX,
                         uint32_t aHotspotY) override;

  CGFloat BackingScaleFactor();
  void BackingScaleFactorChanged();
  virtual double GetDefaultScaleInternal() override;
  virtual int32_t RoundsWidgetCoordinatesTo() override;

  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() final {
    return mozilla::DesktopToLayoutDeviceScale(BackingScaleFactor());
  }

  virtual nsresult SetTitle(const nsAString& aTitle) override;

  virtual void Invalidate(const LayoutDeviceIntRect& aRect) override;
  virtual nsresult ConfigureChildren(const nsTArray<Configuration>& aConfigurations) override;
  virtual LayerManager* GetLayerManager(
      PLayerTransactionChild* aShadowManager = nullptr,
      LayersBackend aBackendHint = mozilla::layers::LayersBackend::LAYERS_NONE,
      LayerManagerPersistence aPersistence = LAYER_MANAGER_CURRENT) override;
  virtual nsresult DispatchEvent(mozilla::WidgetGUIEvent* aEvent, nsEventStatus& aStatus) override;
  virtual void CaptureRollupEvents(nsIRollupListener* aListener, bool aDoCapture) override;
  [[nodiscard]] virtual nsresult GetAttention(int32_t aCycleCount) override;
  virtual bool HasPendingInputEvent() override;
  virtual nsTransparencyMode GetTransparencyMode() override;
  virtual void SetTransparencyMode(nsTransparencyMode aMode) override;
  virtual void SetWindowShadowStyle(mozilla::StyleWindowShadow aStyle) override;
  virtual void SetWindowOpacity(float aOpacity) override;
  virtual void SetWindowTransform(const mozilla::gfx::Matrix& aTransform) override;
  virtual void SetWindowMouseTransparent(bool aIsTransparent) override;
  virtual void SetShowsToolbarButton(bool aShow) override;
  virtual void SetShowsFullScreenButton(bool aShow) override;
  virtual void SetWindowAnimationType(WindowAnimationType aType) override;
  virtual void SetDrawsTitle(bool aDrawTitle) override;
  virtual void SetUseBrightTitlebarForeground(bool aBrightForeground) override;
  virtual nsresult SetNonClientMargins(LayoutDeviceIntMargin& aMargins) override;
  virtual void SetDrawsInTitlebar(bool aState) override;
  virtual void UpdateThemeGeometries(const nsTArray<ThemeGeometry>& aThemeGeometries) override;
  virtual nsresult SynthesizeNativeMouseEvent(LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage,
                                              uint32_t aModifierFlags,
                                              nsIObserver* aObserver) override;

  void DispatchSizeModeEvent();
  void DispatchOcclusionEvent();

  // be notified that a some form of drag event needs to go into Gecko
  virtual bool DragEvent(unsigned int aMessage, mozilla::gfx::Point aMouseGlobal,
                         UInt16 aKeyModifiers);

  bool HasModalDescendents() { return mNumModalDescendents > 0; }
  NSWindow* GetCocoaWindow() { return mWindow; }

  void SetMenuBar(nsMenuBarX* aMenuBar);
  nsMenuBarX* GetMenuBar();

  virtual void SetInputContext(const InputContext& aContext,
                               const InputContextAction& aAction) override;
  virtual InputContext GetInputContext() override { return mInputContext; }
  virtual bool GetEditCommands(NativeKeyBindingsType aType,
                               const mozilla::WidgetKeyboardEvent& aEvent,
                               nsTArray<mozilla::CommandInt>& aCommands) override;

  void SetPopupWindowLevel();

  bool InFullScreenMode() const { return mInFullScreenMode; }

 protected:
  virtual ~nsCocoaWindow();

  nsresult CreateNativeWindow(const NSRect& aRect, nsBorderStyle aBorderStyle,
                              bool aRectIsFrameRect);
  nsresult CreatePopupContentView(const LayoutDeviceIntRect& aRect, nsWidgetInitData* aInitData);
  void DestroyNativeWindow();
  void AdjustWindowShadow();
  void SetWindowBackgroundBlur();
  void UpdateBounds();

  void DoResize(double aX, double aY, double aWidth, double aHeight, bool aRepaint,
                bool aConstrainToCurrentScreen);

  inline bool ShouldToggleNativeFullscreen(bool aFullScreen, bool aUseSystemTransition);
  nsresult DoMakeFullScreen(bool aFullScreen, bool aUseSystemTransition);

  virtual already_AddRefed<nsIWidget> AllocateChildPopupWidget() override {
    return nsIWidget::CreateTopLevelWindow();
  }

  nsIWidget* mParent;         // if we're a popup, this is our parent [WEAK]
  nsIWidget* mAncestorLink;   // link to traverse ancestors [WEAK]
  BaseWindow* mWindow;        // our cocoa window [STRONG]
  WindowDelegate* mDelegate;  // our delegate for processing window msgs [STRONG]
  RefPtr<nsMenuBarX> mMenuBar;
  NSWindow* mSheetWindowParent;    // if this is a sheet, this is the NSWindow it's attached to
  nsChildView* mPopupContentView;  // if this is a popup, this is its content widget
  // if this is a toplevel window, and there is any ongoing fullscreen
  // transition, it is the animation object.
  NSAnimation* mFullscreenTransitionAnimation;
  mozilla::StyleWindowShadow mShadowStyle;

  CGFloat mBackingScaleFactor;

  WindowAnimationType mAnimationType;

  bool mWindowMadeHere;  // true if we created the window, false for embedding
  bool mSheetNeedsShow;  // if this is a sheet, are we waiting to be shown?
                         // this is used for sibling sheet contention only
  bool mInFullScreenMode;
  bool mInFullScreenTransition;  // true from the request to enter/exit fullscreen
                                 // (MakeFullScreen() call) to EnteredFullScreen()
  bool mModal;
  bool mFakeModal;

  // Only true on 10.7+ if SetShowsFullScreenButton(true) is called.
  bool mSupportsNativeFullScreen;
  // Whether we are currently using native fullscreen. It could be false because
  // we are in the DOM fullscreen where we do not use the native fullscreen.
  bool mInNativeFullScreenMode;

  bool mIsAnimationSuppressed;

  bool mInReportMoveEvent;  // true if in a call to ReportMoveEvent().
  bool mInResize;           // true if in a call to DoResize().
  bool mWindowTransformIsIdentity;

  int32_t mNumModalDescendents;
  InputContext mInputContext;
  NSWindowAnimationBehavior mWindowAnimationBehavior;
};

#endif  // nsCocoaWindow_h_
