/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsPrintData_h___
#define nsPrintData_h___

#include "mozilla/Attributes.h"
#include "mozilla/UniquePtr.h"

// Interfaces
#include "nsDeviceContext.h"
#include "nsIPrintSettings.h"
#include "nsISupportsImpl.h"
#include "nsTArray.h"
#include "nsCOMArray.h"

class nsPrintObject;
class nsIPrintProgressParams;
class nsIWebProgressListener;

namespace mozilla {
class PrintPreviewUserEventSuppressor;
}  // namespace mozilla

//------------------------------------------------------------------------
// nsPrintData Class
//
// mPreparingForPrint - indicates that we have started Printing but
//   have not gone to the timer to start printing the pages. It gets turned
//   off right before we go to the timer.
//------------------------------------------------------------------------
class nsPrintData {
  typedef mozilla::PrintPreviewUserEventSuppressor
      PrintPreviewUserEventSuppressor;

 public:
  typedef enum { eIsPrinting, eIsPrintPreview } ePrintDataType;

  explicit nsPrintData(ePrintDataType aType);

  NS_INLINE_DECL_REFCOUNTING(nsPrintData)

  // Listener Helper Methods
  void OnEndPrinting();
  void OnStartPrinting();
  void DoOnProgressChange(int32_t aProgress, int32_t aMaxProgress,
                          bool aDoStartStop, int32_t aFlag);

  void DoOnStatusChange(nsresult aStatus);

  ePrintDataType mType;  // the type of data this is (Printing or Print Preview)
  RefPtr<nsDeviceContext> mPrintDC;

  mozilla::UniquePtr<nsPrintObject> mPrintObject;

  nsCOMArray<nsIWebProgressListener> mPrintProgressListeners;
  nsCOMPtr<nsIPrintProgressParams> mPrintProgressParams;

  nsCOMPtr<nsPIDOMWindowOuter> mCurrentFocusWin;  // cache a pointer to the
                                                  // currently focused window

  // Array of non-owning pointers to all the nsPrintObjects owned by this
  // nsPrintData. This includes this->mPrintObject, as well as all of its
  // mKids (and their mKids, etc.)
  nsTArray<nsPrintObject*> mPrintDocList;

  bool mIsParentAFrameSet;
  bool mOnStartSent;
  bool mIsAborted;          // tells us the document is being aborted
  bool mPreparingForPrint;  // see comments above
  bool mShrinkToFit;
  int32_t mNumPrintablePages;
  int32_t mNumPagesPrinted;
  float mShrinkRatio;

  nsCOMPtr<nsIPrintSettings> mPrintSettings;
  RefPtr<PrintPreviewUserEventSuppressor> mPPEventSuppressor;

 private:
  nsPrintData() = delete;
  nsPrintData& operator=(const nsPrintData& aOther) = delete;

  ~nsPrintData();  // non-virtual
};

#endif /* nsPrintData_h___ */
