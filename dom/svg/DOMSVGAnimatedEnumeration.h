/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_SVG_DOMSVGANIMATEDENUMERATION_H_
#define DOM_SVG_DOMSVGANIMATEDENUMERATION_H_

#include "nsWrapperCache.h"

#include "SVGElement.h"

namespace mozilla {
namespace dom {

class DOMSVGAnimatedEnumeration : public nsISupports, public nsWrapperCache {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(DOMSVGAnimatedEnumeration)

  SVGElement* GetParentObject() const { return mSVGElement; }

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) final;

  virtual uint16_t BaseVal() = 0;
  virtual void SetBaseVal(uint16_t aBaseVal, ErrorResult& aRv) = 0;
  virtual uint16_t AnimVal() = 0;

 protected:
  explicit DOMSVGAnimatedEnumeration(SVGElement* aSVGElement)
      : mSVGElement(aSVGElement) {}
  virtual ~DOMSVGAnimatedEnumeration() = default;

  RefPtr<SVGElement> mSVGElement;
};

}  // namespace dom
}  // namespace mozilla

#endif  // DOM_SVG_DOMSVGANIMATEDENUMERATION_H_
