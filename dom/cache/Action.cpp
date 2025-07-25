/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/cache/Action.h"

namespace mozilla::dom::cache {

void Action::CancelOnInitiatingThread() {
  NS_ASSERT_OWNINGTHREAD(Action);
  // It is possible for cancellation to be duplicated.  For example, an
  // individual Cache could have its Actions canceled and then shutdown
  // could trigger a second action.
  mCanceled = true;
}

Action::Action() : mCanceled(false) {}

Action::~Action() = default;

bool Action::IsCanceled() const { return mCanceled; }

}  // namespace mozilla::dom::cache
