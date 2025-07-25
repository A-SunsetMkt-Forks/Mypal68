/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_indexeddb_keypath_h__
#define mozilla_dom_indexeddb_keypath_h__

#include <new>
#include <utility>
#include "js/TypeDecls.h"
#include "mozilla/Result.h"
#include "nsISupports.h"
#include "nsError.h"
#include "nsString.h"
#include "nsTArray.h"

namespace JS {
template <class T>
class Heap;
}

namespace mozilla {
namespace dom {

class OwningStringOrStringSequence;
template <typename T>
class Sequence;
template <typename T>
struct Nullable;

namespace indexedDB {

class IndexMetadata;
class Key;
class ObjectStoreMetadata;

class KeyPath {
  // This private constructor is only to be used by IPDL-generated classes.
  friend class IndexMetadata;
  friend class ObjectStoreMetadata;

  KeyPath() : mType(NONEXISTENT) { MOZ_COUNT_CTOR(KeyPath); }

 public:
  enum KeyPathType { NONEXISTENT, STRING, ARRAY, ENDGUARD };

  void SetType(KeyPathType aType);

  bool AppendStringWithValidation(const nsAString& aString);

  explicit KeyPath(int aDummy) : mType(NONEXISTENT) { MOZ_COUNT_CTOR(KeyPath); }

  KeyPath(const KeyPath& aOther) {
    MOZ_COUNT_CTOR(KeyPath);
    *this = aOther;
  }

  MOZ_COUNTED_DTOR(KeyPath)

  static nsresult Parse(const nsAString& aString, KeyPath* aKeyPath);

  static nsresult Parse(const Sequence<nsString>& aStrings, KeyPath* aKeyPath);

  static nsresult Parse(const Nullable<OwningStringOrStringSequence>& aValue,
                        KeyPath* aKeyPath);

  nsresult ExtractKey(JSContext* aCx, const JS::Value& aValue, Key& aKey) const;

  nsresult ExtractKeyAsJSVal(JSContext* aCx, const JS::Value& aValue,
                             JS::Value* aOutVal) const;

  typedef nsresult (*ExtractOrCreateKeyCallback)(JSContext* aCx,
                                                 void* aClosure);

  nsresult ExtractOrCreateKey(JSContext* aCx, const JS::Value& aValue,
                              Key& aKey, ExtractOrCreateKeyCallback aCallback,
                              void* aClosure) const;

  inline bool IsValid() const { return mType != NONEXISTENT; }

  inline bool IsArray() const { return mType == ARRAY; }

  inline bool IsString() const { return mType == STRING; }

  inline bool IsEmpty() const {
    return mType == STRING && mStrings[0].IsEmpty();
  }

  bool operator==(const KeyPath& aOther) const {
    return mType == aOther.mType && mStrings == aOther.mStrings;
  }

  void SerializeToString(nsAString& aString) const;
  static KeyPath DeserializeFromString(const nsAString& aString);

  nsresult ToJSVal(JSContext* aCx, JS::MutableHandle<JS::Value> aValue) const;
  nsresult ToJSVal(JSContext* aCx, JS::Heap<JS::Value>& aValue) const;

  bool IsAllowedForObjectStore(bool aAutoIncrement) const;

  KeyPathType mType;

  nsTArray<nsString> mStrings;
};

}  // namespace indexedDB
}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_indexeddb_keypath_h__
