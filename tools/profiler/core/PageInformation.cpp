/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PageInformation.h"

#include "mozilla/ProfileJSONWriter.h"

PageInformation::PageInformation(uint64_t aBrowsingContextID,
                                 uint64_t aInnerWindowID, const nsCString& aUrl,
                                 bool aIsSubFrame)
    : mBrowsingContextID(aBrowsingContextID),
      mInnerWindowID(aInnerWindowID),
      mUrl(aUrl),
      mIsSubFrame(aIsSubFrame) {}

bool PageInformation::Equals(PageInformation* aOtherPageInfo) const {
  // It's enough to check inner window IDs because they are unique for each
  // page. Therefore, we don't have to check browsing context ID or url.
  return InnerWindowID() == aOtherPageInfo->InnerWindowID();
}

void PageInformation::StreamJSON(SpliceableJSONWriter& aWriter) const {
  // Here, we are converting uint64_t to double. Both Browsing Context and Inner
  // Window IDs are creating using `nsContentUtils::GenerateProcessSpecificId`,
  // which is specifically designed to only use 53 of the 64 bits to be lossless
  // when passed into and out of JS as a double.
  aWriter.StartObjectElement();
  aWriter.DoubleProperty("browsingContextID", BrowsingContextID());
  aWriter.DoubleProperty("innerWindowID", InnerWindowID());
  aWriter.StringProperty("url", Url());
  aWriter.BoolProperty("isSubFrame", IsSubFrame());
  aWriter.EndObject();
}

size_t PageInformation::SizeOfIncludingThis(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  return aMallocSizeOf(this);
}
