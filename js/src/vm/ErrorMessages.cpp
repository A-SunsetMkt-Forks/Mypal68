/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* SpiderMonkey-internal error-reporting formatting functionality. */

#include "js/friend/ErrorMessages.h"

#include "jsexn.h"  // js_ErrorFormatString

#include "js/ErrorReport.h"  // JSErrorFormatString

const JSErrorFormatString js_ErrorFormatString[JSErr_Limit] = {
#define MSG_DEF(name, count, exception, format) \
  {#name, format, count, exception},
#include "js/friend/ErrorNumbers.msg"
#undef MSG_DEF
};

const JSErrorFormatString* js::GetErrorMessage(void* userRef,
                                               unsigned errorNumber) {
  if (errorNumber > 0 && errorNumber < JSErr_Limit) {
    return &js_ErrorFormatString[errorNumber];
  }

  return nullptr;
}
