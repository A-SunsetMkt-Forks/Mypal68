/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProcessUtils.h"

#include "mozilla/Preferences.h"
#include "mozilla/ipc/GeckoChildProcessHost.h"
#include "mozilla/RandomNum.h"
#include "mozilla/UniquePtrExtensions.h"

#include "XPCSelfHostedShmem.h"

namespace mozilla {
namespace ipc {

SharedPreferenceSerializer::SharedPreferenceSerializer() : mPrefMapSize(0) {
  MOZ_COUNT_CTOR(SharedPreferenceSerializer);
}

SharedPreferenceSerializer::~SharedPreferenceSerializer() {
  MOZ_COUNT_DTOR(SharedPreferenceSerializer);
}

SharedPreferenceSerializer::SharedPreferenceSerializer(
    SharedPreferenceSerializer&& aOther)
    : mPrefMapSize(aOther.mPrefMapSize),
      mPrefsLength(aOther.mPrefsLength),
      mPrefMapHandle(std::move(aOther.mPrefMapHandle)),
      mPrefsHandle(std::move(aOther.mPrefsHandle)) {
  MOZ_COUNT_CTOR(SharedPreferenceSerializer);
}

bool SharedPreferenceSerializer::SerializeToSharedMemory(
    mozilla::ipc::GeckoChildProcessHost& procHost,
    std::vector<std::string>& aExtraOpts) {

  nsAutoCString name;

  mPrefMapHandle =
      Preferences::EnsureSnapshot(&mPrefMapSize, name).TakePlatformHandle();

  // Serialize the early prefs.
  nsAutoCStringN<1024> prefs;
  Preferences::SerializePreferences(prefs);
  mPrefsLength = prefs.Length();

  nsAutoCString name2;
  name2.SetLength(sizeof("MSM_") + 16 * 2);
  name2.AssignLiteral("MSM_");
  Maybe<uint64_t> randomNum = RandomUint64();
  name2.AppendPrintf("%016llx", *randomNum);

  const char* str;
  name2.GetData(&str);

  base::SharedMemory shm;
  // Set up the shared memory.
  if (!shm.Create(prefs.Length(), str)) {
    NS_ERROR("failed to create shared memory in the parent");
    return false;
  }
  if (!shm.Map(prefs.Length())) {
    NS_ERROR("failed to map shared memory in the parent");
    return false;
  }

  // Copy the serialized prefs into the shared memory.
  memcpy(static_cast<char*>(shm.memory()), prefs.get(), mPrefsLength);

  mPrefsHandle = shm.TakeHandle();
  // Formats a pointer or pointer-sized-integer as a string suitable for passing
  // in an arguments list.
  auto formatPtrArg = [](auto arg) {
    return nsPrintfCString("%zu", uintptr_t(arg));
  };

#if defined(XP_WIN)
  // Record the handle as to-be-shared, and pass it via a command flag. This
  // works because Windows handles are system-wide.
  procHost.AddHandleToShare(GetPrefsHandle().get());
  procHost.AddHandleToShare(GetPrefMapHandle().get());
  aExtraOpts.push_back("-prefsHandle");
  aExtraOpts.push_back(name2.get());
  aExtraOpts.push_back("-prefMapHandle");
  aExtraOpts.push_back(name.get());
#else
  // In contrast, Unix fds are per-process. So remap the fd to a fixed one that
  // will be used in the child.
  // XXX: bug 1440207 is about improving how fixed fds are used.
  //
  // Note: on Android, AddFdToRemap() sets up the fd to be passed via a Parcel,
  // and the fixed fd isn't used. However, we still need to mark it for
  // remapping so it doesn't get closed in the child.
  procHost.AddFdToRemap(GetPrefsHandle().get(), kPrefsFileDescriptor);
  procHost.AddFdToRemap(GetPrefMapHandle().get(), kPrefMapFileDescriptor);
#endif

  // Pass the lengths via command line flags.
  aExtraOpts.push_back("-prefsLen");
  aExtraOpts.push_back(formatPtrArg(GetPrefsLength()).get());
  aExtraOpts.push_back("-prefMapSize");
  aExtraOpts.push_back(formatPtrArg(GetPrefMapSize()).get());

  return true;
}

#ifdef ANDROID
static int gPrefsFd = -1;
static int gPrefMapFd = -1;

void SetPrefsFd(int aFd) { gPrefsFd = aFd; }

void SetPrefMapFd(int aFd) { gPrefMapFd = aFd; }
#endif

SharedPreferenceDeserializer::SharedPreferenceDeserializer() {
  MOZ_COUNT_CTOR(SharedPreferenceDeserializer);
}

SharedPreferenceDeserializer::~SharedPreferenceDeserializer() {
  MOZ_COUNT_DTOR(SharedPreferenceDeserializer);
}

bool SharedPreferenceDeserializer::DeserializeFromSharedMemory(
    char* aPrefsHandleStr, char* aPrefMapHandleStr, char* aPrefsLenStr,
    char* aPrefMapSizeStr) {
#ifdef XP_WIN
  MOZ_ASSERT(aPrefsHandleStr && aPrefMapHandleStr, "Can't be null");
#endif
  MOZ_ASSERT(aPrefsLenStr && aPrefMapSizeStr, "Can't be null");

  // Parses an arg containing a pointer-sized-integer.
  auto parseUIntPtrArg = [](char*& aArg) {
    // ContentParent uses %zu to print a word-sized unsigned integer. So
    // even though strtoull() returns a long long int, it will fit in a
    // uintptr_t.
    return uintptr_t(strtoull(aArg, &aArg, 10));
  };

#ifdef XP_WIN
  mPrefsHandle = Some(::OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, (LPCTSTR)NS_ConvertUTF8toUTF16(aPrefsHandleStr).get()));
  if (!aPrefsHandleStr) {
    return false;
  }

  HANDLE handle=::OpenFileMapping(FILE_MAP_READ, FALSE, (LPCTSTR)NS_ConvertUTF8toUTF16(aPrefMapHandleStr).get());
  if (!aPrefMapHandleStr) {
    return false;
  }

  mPrefMapHandle.emplace(std::move(handle));
#endif

  mPrefsLen = Some(parseUIntPtrArg(aPrefsLenStr));
  if (!aPrefsLenStr || aPrefsLenStr[0] != '\0') {
    return false;
  }

  mPrefMapSize = Some(parseUIntPtrArg(aPrefMapSizeStr));
  if (!aPrefMapSizeStr || aPrefMapSizeStr[0] != '\0') {
    return false;
  }

#ifdef ANDROID
  // Android is different; get the FD via gPrefsFd instead of a fixed fd.
  MOZ_RELEASE_ASSERT(gPrefsFd != -1);
  mPrefsHandle = Some(base::FileDescriptor(gPrefsFd, /* auto_close */ true));

  mPrefMapHandle.emplace(UniqueFileHandle(gPrefMapFd));
#elif XP_UNIX
  mPrefsHandle = Some(base::FileDescriptor(kPrefsFileDescriptor,
                                           /* auto_close */ true));

  mPrefMapHandle.emplace(UniqueFileHandle(kPrefMapFileDescriptor));
#endif

  if (mPrefsHandle.isNothing() || mPrefsLen.isNothing() ||
      mPrefMapHandle.isNothing() || mPrefMapSize.isNothing()) {
    return false;
  }

  // Init the shared-memory base preference mapping first, so that only changed
  // preferences wind up in heap memory.
  Preferences::InitSnapshot(mPrefMapHandle.ref(), *mPrefMapSize);

  // Set up early prefs from the shared memory.
  if (!mShmem.SetHandle(*mPrefsHandle, /* read_only */ true)) {
    NS_ERROR("failed to open shared memory in the child");
    return false;
  }
  if (!mShmem.Map(*mPrefsLen)) {
    NS_ERROR("failed to map shared memory in the child");
    return false;
  }
  Preferences::DeserializePreferences(static_cast<char*>(mShmem.memory()),
                                      *mPrefsLen);

  return true;
}

const base::SharedMemoryHandle& SharedPreferenceDeserializer::GetPrefsHandle()
    const {
  MOZ_ASSERT(mPrefsHandle.isSome());

  return mPrefsHandle.ref();
}

const FileDescriptor& SharedPreferenceDeserializer::GetPrefMapHandle() const {
  MOZ_ASSERT(mPrefMapHandle.isSome());

  return mPrefMapHandle.ref();
}

#ifdef XP_UNIX
// On Unix, file descriptors are per-process. This value is used when mapping
// a parent process handle to a content process handle.
static const int kJSInitFileDescriptor = 11;
#endif

void ExportSharedJSInit(mozilla::ipc::GeckoChildProcessHost& procHost,
                        std::vector<std::string>& aExtraOpts) {
#ifdef ANDROID
  // The code to support Android is added in a follow-up patch.
  return;
#else
  // Formats a pointer or pointer-sized-integer as a string suitable for passing
  // in an arguments list.
  auto formatPtrArg = [](auto arg) {
    return nsPrintfCString("%zu", uintptr_t(arg));
  };

  auto& shmem = xpc::SelfHostedShmem::GetSingleton();
  const mozilla::UniqueFileHandle& uniqHandle = shmem.Handle();
  size_t len = shmem.Content().Length();

  // If the file is not found or the content is empty, then we would start the
  // content process without this optimization.
  if (!uniqHandle || !len) {
    return;
  }

  mozilla::detail::FileHandleType handle = uniqHandle.get();

  // command line: -jsInit [handle] length
  aExtraOpts.push_back("-jsInit");

#  if defined(XP_WIN)
  // Record the handle as to-be-shared, and pass it via a command flag.
  procHost.AddHandleToShare(HANDLE(handle));
  aExtraOpts.push_back(formatPtrArg(HANDLE(handle)).get());
#  else
  // In contrast, Unix fds are per-process. So remap the fd to a fixed one that
  // will be used in the child.
  // XXX: bug 1440207 is about improving how fixed fds are used.
  //
  // Note: on Android, AddFdToRemap() sets up the fd to be passed via a Parcel,
  // and the fixed fd isn't used. However, we still need to mark it for
  // remapping so it doesn't get closed in the child.
  procHost.AddFdToRemap(handle, kJSInitFileDescriptor);
#  endif

  // Pass the lengths via command line flags.
  aExtraOpts.push_back(formatPtrArg(len).get());
#endif
}

bool ImportSharedJSInit(char* aJsInitHandleStr, char* aJsInitLenStr) {
  // This is an optimization, and as such we can safely recover if the command
  // line argument are not provided.
  if (!aJsInitLenStr) {
    return true;
  }

#ifdef XP_WIN
  if (!aJsInitHandleStr) {
    return true;
  }
#endif

  // Parses an arg containing a pointer-sized-integer.
  auto parseUIntPtrArg = [](char*& aArg) {
    // ContentParent uses %zu to print a word-sized unsigned integer. So even
    // though strtoull() returns an unsigned long long int, it will fit in a
    // uintptr_t.
    return uintptr_t(strtoull(aArg, &aArg, 10));
  };

#ifdef XP_WIN
  auto parseHandleArg = [&](char*& aArg) {
    return HANDLE(parseUIntPtrArg(aArg));
  };

  base::SharedMemoryHandle handle(parseHandleArg(aJsInitHandleStr));
  if (aJsInitHandleStr[0] != '\0') {
    return false;
  }
#endif

  size_t len = parseUIntPtrArg(aJsInitLenStr);
  if (aJsInitLenStr[0] != '\0') {
    return false;
  }

#ifdef XP_UNIX
  auto handle = base::FileDescriptor(kJSInitFileDescriptor,
                                     /* auto_close */ true);
#endif

  // Initialize the shared memory with the file handle and size of the content
  // of the self-hosted Xdr.
  auto& shmem = xpc::SelfHostedShmem::GetSingleton();
  if (!shmem.InitFromChild(handle, len)) {
    NS_ERROR("failed to open shared memory in the child");
    return false;
  }

  return true;
}

}  // namespace ipc
}  // namespace mozilla
