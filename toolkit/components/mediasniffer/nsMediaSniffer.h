/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsMediaSniffer_h
#define nsMediaSniffer_h


#include "nsIContentSniffer.h"
#include "mozilla/Attributes.h"

// ed905ba3-c656-480e-934e-6bc35bd36aff
#define NS_MEDIA_SNIFFER_CID                         \
  {                                                  \
    0x3fdd6c28, 0x5b87, 0x4e3e, {                    \
      0x8b, 0x57, 0x8e, 0x83, 0xc2, 0x3c, 0x1a, 0x6d \
    }                                                \
  }

#define NS_MEDIA_SNIFFER_CONTRACTID "@mozilla.org/media/sniffer;1"

#define PATTERN_ENTRY(mask, pattern, contentType)                    \
  {                                                                  \
    (const uint8_t*)mask, (const uint8_t*)pattern, sizeof(mask) - 1, \
        contentType                                                  \
  }

struct nsMediaSnifferEntry {
  const uint8_t* mMask;
  const uint8_t* mPattern;
  const uint32_t mLength;
  const char* mContentType;
};

class nsMediaSniffer final : public nsIContentSniffer {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICONTENTSNIFFER

 private:
  ~nsMediaSniffer() = default;

  static nsMediaSnifferEntry sSnifferEntries[];
};

#endif
