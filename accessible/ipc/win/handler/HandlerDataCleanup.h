/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_a11y_HandlerDataCleanup_h
#define mozilla_a11y_HandlerDataCleanup_h

#include <oleauto.h>

namespace mozilla {
namespace a11y {

inline void ReleaseStaticIA2DataInterfaces(StaticIA2Data& aData) {
  // Only interfaces of the proxied object wrapped by this handler should be
  // released here, never other objects!
  // For example, if StaticIA2Data were to include accParent in future,
  // that must not be released here.
  if (aData.mIA2) {
    aData.mIA2->Release();
  }
  if (aData.mIAHypertext) {
    aData.mIAHypertext->Release();
  }
  if (aData.mIAHyperlink) {
    aData.mIAHyperlink->Release();
  }
  if (aData.mIATable) {
    aData.mIATable->Release();
  }
  if (aData.mIATable2) {
    aData.mIATable2->Release();
  }
  if (aData.mIATableCell) {
    aData.mIATableCell->Release();
  }
}

inline void CleanupDynamicIA2Data(DynamicIA2Data& aData, bool aZero = true) {
  ::VariantClear(&aData.mRole);
  if (aData.mKeyboardShortcut) {
    ::SysFreeString(aData.mKeyboardShortcut);
  }
  if (aData.mName) {
    ::SysFreeString(aData.mName);
  }
  if (aData.mDescription) {
    ::SysFreeString(aData.mDescription);
  }
  if (aData.mDefaultAction) {
    ::SysFreeString(aData.mDefaultAction);
  }
  if (aData.mValue) {
    ::SysFreeString(aData.mValue);
  }
  if (aData.mAttributes) {
    ::SysFreeString(aData.mAttributes);
  }
  if (aData.mIA2Locale.language) {
    ::SysFreeString(aData.mIA2Locale.language);
  }
  if (aData.mIA2Locale.country) {
    ::SysFreeString(aData.mIA2Locale.country);
  }
  if (aData.mIA2Locale.variant) {
    ::SysFreeString(aData.mIA2Locale.variant);
  }
  if (aZero) {
    ZeroMemory(&aData, sizeof(DynamicIA2Data));
  }
}

}  // namespace a11y
}  // namespace mozilla

#endif  // mozilla_a11y_HandlerDataCleanup_h
