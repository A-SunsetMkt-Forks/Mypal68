/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsLegendFrame_h___
#define nsLegendFrame_h___

#include "mozilla/Attributes.h"
#include "nsBlockFrame.h"

class nsLegendFrame final : public nsBlockFrame {
 public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(nsLegendFrame)

  explicit nsLegendFrame(ComputedStyle* aStyle, nsPresContext* aPresContext)
      : nsBlockFrame(aStyle, aPresContext, kClassID) {}

  virtual void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
                      const ReflowInput& aReflowInput,
                      nsReflowStatus& aStatus) override;

  virtual void DestroyFrom(nsIFrame* aDestructRoot,
                           PostDestroyData& aPostDestroyData) override;

#ifdef DEBUG_FRAME_DUMP
  virtual nsresult GetFrameName(nsAString& aResult) const override;
#endif

  HTMLLegendElement::LegendAlignValue GetLogicalAlign(
      mozilla::WritingMode aCBWM);
};

#endif  // guard
