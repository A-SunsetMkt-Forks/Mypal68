/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/TextEncoder.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/UniquePtrExtensions.h"
#include "nsReadableUtils.h"

namespace mozilla::dom {

void TextEncoder::Encode(JSContext* aCx, JS::Handle<JSObject*> aObj,
                         const nsACString& aUtf8String,
                         JS::MutableHandle<JSObject*> aRetval,
                         OOMReporter& aRv) {
  // Uint8Array::Create takes uint32_t as the length.
  if (MOZ_UNLIKELY(aUtf8String.Length() > UINT32_MAX)) {
    aRv.ReportOOM();
    return;
  }

  JSAutoRealm ar(aCx, aObj);
  const char* buffer = aUtf8String.BeginReading();
  JSObject* outView = Uint8Array::Create(aCx, aUtf8String.Length(), reinterpret_cast<const uint8_t*>(buffer));
  if (!outView) {
    aRv.ReportOOM();
    return;
  }

  aRetval.set(outView);
}

void TextEncoder::EncodeInto(JSContext* aCx, JS::Handle<JSString*> aSrc,
                             const Uint8Array& aDst,
                             TextEncoderEncodeIntoResult& aResult,
                             OOMReporter& aError) {
  aDst.ComputeState();
  size_t read;
  size_t written;
  auto maybe = JS_EncodeStringToUTF8BufferPartial(
      aCx, aSrc, AsWritableChars(Span(aDst.Data(), aDst.Length())));
  if (!maybe) {
    aError.ReportOOM();
    return;
  }
  Tie(read, written) = *maybe;
  MOZ_ASSERT(written <= aDst.Length());
  aResult.mRead.Construct() = read;
  aResult.mWritten.Construct() = written;
}

void TextEncoder::GetEncoding(nsACString& aEncoding) {
  aEncoding.AssignLiteral("utf-8");
}

}  // namespace mozilla::dom
