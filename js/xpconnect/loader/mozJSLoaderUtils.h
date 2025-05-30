/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozJSLoaderUtils_h
#define mozJSLoaderUtils_h

#include "nsString.h"

#include "js/CompileOptions.h"  // JS::ReadOnlyCompileOptions

namespace mozilla {
namespace scache {
class StartupCache;
}  // namespace scache
}  // namespace mozilla

nsresult ReadCachedScript(mozilla::scache::StartupCache* cache, nsACString& uri,
                          JSContext* cx,
                          const JS::ReadOnlyCompileOptions& options,
                          JS::MutableHandleScript scriptp);

nsresult WriteCachedScript(mozilla::scache::StartupCache* cache,
                           nsACString& uri, JSContext* cx,
                           JS::HandleScript script);

#endif /* mozJSLoaderUtils_h */
