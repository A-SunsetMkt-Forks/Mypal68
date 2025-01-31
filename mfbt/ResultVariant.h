/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A type suitable for returning either a value or an error from a function. */

#ifndef mozilla_ResultVariant_h
#define mozilla_ResultVariant_h

#include "mozilla/Result.h"
#include "mozilla/Variant.h"

namespace mozilla::detail {

template <typename V, typename E>
class ResultImplementation<V, E, PackingStrategy::Variant> {
  mozilla::Variant<V, E> mStorage;

 public:
  ResultImplementation(ResultImplementation&&) = default;
  ResultImplementation(const ResultImplementation&) = delete;
  ResultImplementation& operator=(const ResultImplementation&) = delete;
  ResultImplementation& operator=(ResultImplementation&&) = default;

  explicit ResultImplementation(V&& aValue)
      : mStorage(std::forward<V>(aValue)) {}
  explicit ResultImplementation(const V& aValue) : mStorage(aValue) {}
  template <typename... Args>
  explicit ResultImplementation(std::in_place_t, Args&&... aArgs)
      : mStorage(VariantType<V>{}, std::forward<Args>(aArgs)...) {}

  explicit ResultImplementation(const E& aErrorValue) : mStorage(aErrorValue) {}
  explicit ResultImplementation(E&& aErrorValue)
      : mStorage(std::forward<E>(aErrorValue)) {}

  bool isOk() const { return mStorage.template is<V>(); }

  // The callers of these functions will assert isOk() has the proper value, so
  // these functions (in all ResultImplementation specializations) don't need
  // to do so.
  V unwrap() { return std::move(mStorage.template as<V>()); }
  const V& inspect() const { return mStorage.template as<V>(); }

  E unwrapErr() { return std::move(mStorage.template as<E>()); }
  const E& inspectErr() const { return mStorage.template as<E>(); }
};

}  // namespace mozilla::detail

#endif  // mozilla_ResultVariant_h
