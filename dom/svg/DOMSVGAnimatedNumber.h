/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_DOMSVGANIMATEDNUMBER_H_
#define DOM_SVG_DOMSVGANIMATEDNUMBER_H_

#include "nsISupports.h"
#include "nsWrapperCache.h"

#include "SVGElement.h"

namespace mozilla {
namespace dom {

class DOMSVGAnimatedNumber : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMSVGAnimatedNumber)

  SVGElement* GetParentObject() const { return mSVGElement; }

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) final;

  virtual float BaseVal() = 0;
  virtual void SetBaseVal(float aBaseVal) = 0;
  virtual float AnimVal() = 0;

 protected:
  explicit DOMSVGAnimatedNumber(SVGElement* aSVGElement)
      : mSVGElement(aSVGElement) {}
  virtual ~DOMSVGAnimatedNumber() = default;

  RefPtr<SVGElement> mSVGElement;
};

}  // namespace dom
}  // namespace mozilla

#endif  // DOM_SVG_DOMSVGANIMATEDNUMBER_H_
