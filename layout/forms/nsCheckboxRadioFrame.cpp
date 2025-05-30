/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCheckboxRadioFrame.h"

#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/PresShell.h"
#include "nsDeviceContext.h"
#include "nsIContent.h"
#include "nsStyleConsts.h"

using namespace mozilla;
using mozilla::dom::Element;
using mozilla::dom::HTMLInputElement;

//#define FCF_NOISY

nsCheckboxRadioFrame* NS_NewCheckboxRadioFrame(PresShell* aPresShell,
                                               ComputedStyle* aStyle) {
  return new (aPresShell)
      nsCheckboxRadioFrame(aStyle, aPresShell->GetPresContext());
}

nsCheckboxRadioFrame::nsCheckboxRadioFrame(ComputedStyle* aStyle,
                                           nsPresContext* aPresContext)
    : nsAtomicContainerFrame(aStyle, aPresContext, kClassID) {}

nsCheckboxRadioFrame::~nsCheckboxRadioFrame() = default;

void nsCheckboxRadioFrame::DestroyFrom(nsIFrame* aDestructRoot,
                                       PostDestroyData& aPostDestroyData) {
  // Unregister the access key registered in reflow
  nsCheckboxRadioFrame::RegUnRegAccessKey(static_cast<nsIFrame*>(this), false);
  nsAtomicContainerFrame::DestroyFrom(aDestructRoot, aPostDestroyData);
}

NS_IMPL_FRAMEARENA_HELPERS(nsCheckboxRadioFrame)

NS_QUERYFRAME_HEAD(nsCheckboxRadioFrame)
  NS_QUERYFRAME_ENTRY(nsIFormControlFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsAtomicContainerFrame)

/* virtual */
nscoord nsCheckboxRadioFrame::GetMinISize(gfxContext* aRenderingContext) {
  nscoord result;
  DISPLAY_MIN_INLINE_SIZE(this, result);
  result = StyleDisplay()->HasAppearance() ? DefaultSize() : 0;
  return result;
}

/* virtual */
nscoord nsCheckboxRadioFrame::GetPrefISize(gfxContext* aRenderingContext) {
  nscoord result;
  DISPLAY_PREF_INLINE_SIZE(this, result);
  result = StyleDisplay()->HasAppearance() ? DefaultSize() : 0;
  return result;
}

/* virtual */
LogicalSize nsCheckboxRadioFrame::ComputeAutoSize(
    gfxContext* aRC, WritingMode aWM, const LogicalSize& aCBSize,
    nscoord aAvailableISize, const LogicalSize& aMargin,
    const LogicalSize& aBorderPadding, const StyleSizeOverrides& aSizeOverrides,
    ComputeSizeFlags aFlags) {
  LogicalSize size(aWM, 0, 0);
  if (!StyleDisplay()->HasAppearance()) {
    return size;
  }

  // Note: this call always set the BSize to NS_UNCONSTRAINEDSIZE.
  size = nsAtomicContainerFrame::ComputeAutoSize(
      aRC, aWM, aCBSize, aAvailableISize, aMargin, aBorderPadding,
      aSizeOverrides, aFlags);
  size.BSize(aWM) = DefaultSize();
  return size;
}

nscoord nsCheckboxRadioFrame::GetLogicalBaseline(
    WritingMode aWritingMode) const {
  NS_ASSERTION(!IsSubtreeDirty(), "frame must not be dirty");

  // For appearance:none we use a standard CSS baseline, i.e. synthesized from
  // our margin-box.
  if (!StyleDisplay()->HasAppearance()) {
    return nsAtomicContainerFrame::GetLogicalBaseline(aWritingMode);
  }

  // This is for compatibility with Chrome, Safari and Edge (Dec 2016).
  // Treat radio buttons and checkboxes as having an intrinsic baseline
  // at the block-end of the control (use the block-end content edge rather
  // than the margin edge).
  // For "inverted" lines (typically in writing-mode:vertical-lr), use the
  // block-start end instead.
  return aWritingMode.IsLineInverted()
             ? GetLogicalUsedBorderAndPadding(aWritingMode).BStart(aWritingMode)
             : BSize(aWritingMode) -
                   GetLogicalUsedBorderAndPadding(aWritingMode)
                       .BEnd(aWritingMode);
}

void nsCheckboxRadioFrame::Reflow(nsPresContext* aPresContext,
                                  ReflowOutput& aDesiredSize,
                                  const ReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("nsCheckboxRadioFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowInput, aDesiredSize, aStatus);
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");
  NS_FRAME_TRACE(
      NS_FRAME_TRACE_CALLS,
      ("enter nsCheckboxRadioFrame::Reflow: aMaxSize=%d,%d",
       aReflowInput.AvailableWidth(), aReflowInput.AvailableHeight()));

  if (mState & NS_FRAME_FIRST_REFLOW) {
    RegUnRegAccessKey(static_cast<nsIFrame*>(this), true);
  }

  const auto wm = aReflowInput.GetWritingMode();
  aDesiredSize.SetSize(wm, aReflowInput.ComputedSizeWithBorderPadding(wm));

  if (nsLayoutUtils::FontSizeInflationEnabled(aPresContext)) {
    float inflation = nsLayoutUtils::FontSizeInflationFor(this);
    aDesiredSize.Width() *= inflation;
    aDesiredSize.Height() *= inflation;
  }

  NS_FRAME_TRACE(NS_FRAME_TRACE_CALLS,
                 ("exit nsCheckboxRadioFrame::Reflow: size=%d,%d",
                  aDesiredSize.Width(), aDesiredSize.Height()));
  NS_FRAME_SET_TRUNCATION(aStatus, aReflowInput, aDesiredSize);

  aDesiredSize.SetOverflowAreasToDesiredBounds();
  FinishAndStoreOverflow(&aDesiredSize);
}

nsresult nsCheckboxRadioFrame::RegUnRegAccessKey(nsIFrame* aFrame,
                                                 bool aDoReg) {
  NS_ENSURE_ARG_POINTER(aFrame);

  nsPresContext* presContext = aFrame->PresContext();

  NS_ASSERTION(presContext, "aPresContext is NULL in RegUnRegAccessKey!");

  nsAutoString accessKey;

  Element* content = aFrame->GetContent()->AsElement();
  content->GetAttr(kNameSpaceID_None, nsGkAtoms::accesskey, accessKey);
  if (!accessKey.IsEmpty()) {
    EventStateManager* stateManager = presContext->EventStateManager();
    if (aDoReg) {
      stateManager->RegisterAccessKey(content, (uint32_t)accessKey.First());
    } else {
      stateManager->UnregisterAccessKey(content, (uint32_t)accessKey.First());
    }
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

void nsCheckboxRadioFrame::SetFocus(bool aOn, bool aRepaint) {}

nsresult nsCheckboxRadioFrame::HandleEvent(nsPresContext* aPresContext,
                                           WidgetGUIEvent* aEvent,
                                           nsEventStatus* aEventStatus) {
  // Check for disabled content so that selection works properly (?).
  if (IsContentDisabled()) {
    return nsIFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  }
  return NS_OK;
}

void nsCheckboxRadioFrame::GetCurrentCheckState(bool* aState) {
  HTMLInputElement* inputElement = HTMLInputElement::FromNode(mContent);
  if (inputElement) {
    *aState = inputElement->Checked();
  }
}

nsresult nsCheckboxRadioFrame::SetFormProperty(nsAtom* aName,
                                               const nsAString& aValue) {
  return NS_OK;
}

// static
nsRect nsCheckboxRadioFrame::GetUsableScreenRect(nsPresContext* aPresContext) {
  nsRect screen;

  nsDeviceContext* context = aPresContext->DeviceContext();
  int32_t dropdownCanOverlapOSBar =
      LookAndFeel::GetInt(LookAndFeel::IntID::MenusCanOverlapOSBar, 0);
  if (dropdownCanOverlapOSBar)
    context->GetRect(screen);
  else
    context->GetClientRect(screen);

  return screen;
}
