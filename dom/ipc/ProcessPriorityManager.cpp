/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ProcessPriorityManager.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/Hal.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Unused.h"
#include "mozilla/Logging.h"
#include "nsPrintfCString.h"
#include "nsXULAppAPI.h"
#include "nsFrameLoader.h"
#include "nsINamed.h"
#include "nsIObserverService.h"
#include "StaticPtr.h"
#include "nsIObserver.h"
#include "nsITimer.h"
#include "nsIPropertyBag2.h"
#include "nsComponentManagerUtils.h"
#include "nsCRT.h"
#include "nsTHashtable.h"
#include "nsQueryObject.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::hal;

#ifdef XP_WIN
#  include <process.h>
#  define getpid _getpid
#else
#  include <unistd.h>
#endif

#ifdef LOG
#  undef LOG
#endif

// Use LOGP inside a ParticularProcessPriorityManager method; use LOG
// everywhere else.  LOGP prints out information about the particular process
// priority manager.
//
// (Wow, our logging story is a huge mess.)

// #define ENABLE_LOGGING 1

#if defined(ANDROID) && defined(ENABLE_LOGGING)
#  include <android/log.h>
#  define LOG(fmt, ...)                                                        \
    __android_log_print(ANDROID_LOG_INFO, "Gecko:ProcessPriorityManager", fmt, \
                        ##__VA_ARGS__)
#  define LOGP(fmt, ...)                                                \
    __android_log_print(                                                \
        ANDROID_LOG_INFO, "Gecko:ProcessPriorityManager",               \
        "[%schild-id=%" PRIu64 ", pid=%d] " fmt, NameWithComma().get(), \
        static_cast<uint64_t>(ChildID()), Pid(), ##__VA_ARGS__)

#elif defined(ENABLE_LOGGING)
#  define LOG(fmt, ...) \
    printf("ProcessPriorityManager - " fmt "\n", ##__VA_ARGS__)
#  define LOGP(fmt, ...)                                                   \
    printf("ProcessPriorityManager[%schild-id=%" PRIu64 ", pid=%d] - " fmt \
           "\n",                                                           \
           NameWithComma().get(), static_cast<uint64_t>(ChildID()), Pid(), \
           ##__VA_ARGS__)
#else
static LogModule* GetPPMLog() {
  static LazyLogModule sLog("ProcessPriorityManager");
  return sLog;
}
#  define LOG(fmt, ...)                   \
    MOZ_LOG(GetPPMLog(), LogLevel::Debug, \
            ("ProcessPriorityManager - " fmt, ##__VA_ARGS__))
#  define LOGP(fmt, ...)                                                      \
    MOZ_LOG(GetPPMLog(), LogLevel::Debug,                                     \
            ("ProcessPriorityManager[%schild-id=%" PRIu64 ", pid=%d] - " fmt, \
             NameWithComma().get(), static_cast<uint64_t>(ChildID()), Pid(),  \
             ##__VA_ARGS__))
#endif

namespace {

class ParticularProcessPriorityManager;

/**
 * This singleton class does the work to implement the process priority manager
 * in the main process.  This class may not be used in child processes.  (You
 * can call StaticInit, but it won't do anything, and GetSingleton() will
 * return null.)
 *
 * ProcessPriorityManager::CurrentProcessIsForeground() and
 * ProcessPriorityManager::AnyProcessHasHighPriority() which can be called in
 * any process, are handled separately, by the ProcessPriorityManagerChild
 * class.
 */
class ProcessPriorityManagerImpl final : public nsIObserver,
                                         public nsSupportsWeakReference {
 public:
  /**
   * If we're in the main process, get the ProcessPriorityManagerImpl
   * singleton.  If we're in a child process, return null.
   */
  static ProcessPriorityManagerImpl* GetSingleton();

  static void StaticInit();
  static bool PrefsEnabled();
  static bool TestMode();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  /**
   * This function implements ProcessPriorityManager::SetProcessPriority.
   */
  void SetProcessPriority(ContentParent* aContentParent,
                          ProcessPriority aPriority);

  /**
   * If a magic testing-only pref is set, notify the observer service on the
   * given topic with the given data.  This is used for testing
   */
  void FireTestOnlyObserverNotification(
      const char* aTopic, const nsACString& aData = EmptyCString());

  /**
   * This must be called by a ParticularProcessPriorityManager when it changes
   * its priority.
   */
  void NotifyProcessPriorityChanged(
      ParticularProcessPriorityManager* aParticularManager,
      hal::ProcessPriority aOldPriority);

  void TabActivityChanged(BrowserParent* aBrowserParent, bool aIsActive);

 private:
  static bool sPrefListenersRegistered;
  static bool sInitialized;
  static StaticRefPtr<ProcessPriorityManagerImpl> sSingleton;

  static void PrefChangedCallback(const char* aPref, void* aClosure);

  ProcessPriorityManagerImpl();
  ~ProcessPriorityManagerImpl();
  DISALLOW_EVIL_CONSTRUCTORS(ProcessPriorityManagerImpl);

  void Init();

  already_AddRefed<ParticularProcessPriorityManager>
  GetParticularProcessPriorityManager(ContentParent* aContentParent);

  void ObserveContentParentCreated(nsISupports* aContentParent);
  void ObserveContentParentDestroyed(nsISupports* aSubject);

  nsDataHashtable<nsUint64HashKey, RefPtr<ParticularProcessPriorityManager> >
      mParticularManagers;

  /** Contains the PIDs of child processes holding high-priority wakelocks */
  nsTHashtable<nsUint64HashKey> mHighPriorityChildIDs;
};

/**
 * This singleton class implements the parts of the process priority manager
 * that are available from all processes.
 */
class ProcessPriorityManagerChild final : public nsIObserver {
 public:
  static void StaticInit();
  static ProcessPriorityManagerChild* Singleton();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  bool CurrentProcessIsForeground();

 private:
  static StaticRefPtr<ProcessPriorityManagerChild> sSingleton;

  ProcessPriorityManagerChild();
  ~ProcessPriorityManagerChild() = default;
  DISALLOW_EVIL_CONSTRUCTORS(ProcessPriorityManagerChild);

  void Init();

  hal::ProcessPriority mCachedPriority;
};

/**
 * This class manages the priority of one particular process.  It is
 * main-process only.
 */
class ParticularProcessPriorityManager final : public WakeLockObserver,
                                               public nsIObserver,
                                               public nsITimerCallback,
                                               public nsINamed,
                                               public nsSupportsWeakReference {
  ~ParticularProcessPriorityManager();

 public:
  explicit ParticularProcessPriorityManager(ContentParent* aContentParent);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITIMERCALLBACK

  virtual void Notify(const WakeLockInformation& aInfo) override;
  void Init();

  int32_t Pid() const;
  uint64_t ChildID() const;

  /**
   * Used in logging, this method returns the ContentParent's name followed by
   * ", ".  If we can't get the ContentParent's name for some reason, it
   * returns an empty string.
   *
   * The reference returned here is guaranteed to be live until the next call
   * to NameWithComma() or until the ParticularProcessPriorityManager is
   * destroyed, whichever comes first.
   */
  const nsAutoCString& NameWithComma();

  void OnRemoteBrowserFrameShown(nsISupports* aSubject);
  void OnBrowserParentDestroyed(nsISupports* aSubject);

  ProcessPriority CurrentPriority();
  ProcessPriority ComputePriority();

  enum TimeoutPref {
    BACKGROUND_PERCEIVABLE_GRACE_PERIOD,
    BACKGROUND_GRACE_PERIOD,
  };

  void ScheduleResetPriority(TimeoutPref aTimeoutPref);
  void ResetPriority();
  void ResetPriorityNow();
  void SetPriorityNow(ProcessPriority aPriority);

  void TabActivityChanged(BrowserParent* aBrowserParent, bool aIsActive);

  void ShutDown();

  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("ParticularProcessPriorityManager");
    return NS_OK;
  }

 private:
  void FireTestOnlyObserverNotification(
      const char* aTopic, const nsACString& aData = EmptyCString());

  void FireTestOnlyObserverNotification(const char* aTopic,
                                        const char* aData = nullptr);

  bool IsHoldingWakeLock(const nsAString& aTopic);

  ContentParent* mContentParent;
  uint64_t mChildID;
  ProcessPriority mPriority;
  bool mHoldsCPUWakeLock;
  bool mHoldsHighPriorityWakeLock;
  bool mHoldsPlayingAudioWakeLock;
  bool mHoldsPlayingVideoWakeLock;

  /**
   * Used to implement NameWithComma().
   */
  nsAutoCString mNameWithComma;

  nsCOMPtr<nsITimer> mResetPriorityTimer;

  // This hashtable contains the list of active TabId for this process.
  nsTHashtable<nsUint64HashKey> mActiveBrowserParents;
};

/* static */
bool ProcessPriorityManagerImpl::sInitialized = false;
/* static */
bool ProcessPriorityManagerImpl::sPrefListenersRegistered = false;
/* static */
StaticRefPtr<ProcessPriorityManagerImpl> ProcessPriorityManagerImpl::sSingleton;

NS_IMPL_ISUPPORTS(ProcessPriorityManagerImpl, nsIObserver,
                  nsISupportsWeakReference);

/* static */
void ProcessPriorityManagerImpl::PrefChangedCallback(const char* aPref,
                                                     void* aClosure) {
  StaticInit();
  if (!PrefsEnabled() && sSingleton) {
    sSingleton = nullptr;
    sInitialized = false;
  }
}

/* static */
bool ProcessPriorityManagerImpl::PrefsEnabled() {
  return StaticPrefs::dom_ipc_processPriorityManager_enabled() &&
         hal::SetProcessPrioritySupported() &&
         !StaticPrefs::dom_ipc_tabs_disabled();
}

/* static */
bool ProcessPriorityManagerImpl::TestMode() {
  return StaticPrefs::dom_ipc_processPriorityManager_testMode();
}

/* static */
void ProcessPriorityManagerImpl::StaticInit() {
  if (sInitialized) {
    return;
  }

  // The process priority manager is main-process only.
  if (!XRE_IsParentProcess()) {
    sInitialized = true;
    return;
  }

  // If IPC tabs aren't enabled at startup, don't bother with any of this.
  if (!PrefsEnabled()) {
    LOG("InitProcessPriorityManager bailing due to prefs.");

    // Run StaticInit() again if the prefs change.  We don't expect this to
    // happen in normal operation, but it happens during testing.
    if (!sPrefListenersRegistered) {
      sPrefListenersRegistered = true;
      Preferences::RegisterCallback(PrefChangedCallback,
                                    "dom.ipc.processPriorityManager.enabled");
      Preferences::RegisterCallback(PrefChangedCallback,
                                    "dom.ipc.tabs.disabled");
    }
    return;
  }

  sInitialized = true;

  sSingleton = new ProcessPriorityManagerImpl();
  sSingleton->Init();
  ClearOnShutdown(&sSingleton);
}

/* static */
ProcessPriorityManagerImpl* ProcessPriorityManagerImpl::GetSingleton() {
  if (!sSingleton) {
    StaticInit();
  }

  return sSingleton;
}

ProcessPriorityManagerImpl::ProcessPriorityManagerImpl() {
  MOZ_ASSERT(XRE_IsParentProcess());
}

ProcessPriorityManagerImpl::~ProcessPriorityManagerImpl() = default;

void ProcessPriorityManagerImpl::Init() {
  LOG("Starting up.  This is the master process.");

  // The master process's priority never changes; set it here and then forget
  // about it.  We'll manage only subprocesses' priorities using the process
  // priority manager.
  hal::SetProcessPriority(getpid(), PROCESS_PRIORITY_MASTER);

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->AddObserver(this, "ipc:content-created", /* ownsWeak */ true);
    os->AddObserver(this, "ipc:content-shutdown", /* ownsWeak */ true);
  }
}

NS_IMETHODIMP
ProcessPriorityManagerImpl::Observe(nsISupports* aSubject, const char* aTopic,
                                    const char16_t* aData) {
  nsDependentCString topic(aTopic);
  if (topic.EqualsLiteral("ipc:content-created")) {
    ObserveContentParentCreated(aSubject);
  } else if (topic.EqualsLiteral("ipc:content-shutdown")) {
    ObserveContentParentDestroyed(aSubject);
  } else {
    MOZ_ASSERT(false);
  }

  return NS_OK;
}

already_AddRefed<ParticularProcessPriorityManager>
ProcessPriorityManagerImpl::GetParticularProcessPriorityManager(
    ContentParent* aContentParent) {
  uint64_t cpId = aContentParent->ChildID();
  auto entry = mParticularManagers.LookupForAdd(cpId);
  RefPtr<ParticularProcessPriorityManager> pppm =
      entry.OrInsert([aContentParent]() {
        return new ParticularProcessPriorityManager(aContentParent);
      });

  if (!entry) {
    // We created a new entry.
    pppm->Init();
    FireTestOnlyObserverNotification("process-created",
                                     nsPrintfCString("%" PRIu64, cpId));
  }

  return pppm.forget();
}

void ProcessPriorityManagerImpl::SetProcessPriority(
    ContentParent* aContentParent, ProcessPriority aPriority) {
  MOZ_ASSERT(aContentParent);
  RefPtr<ParticularProcessPriorityManager> pppm =
      GetParticularProcessPriorityManager(aContentParent);
  if (pppm) {
    pppm->SetPriorityNow(aPriority);
  }
}

void ProcessPriorityManagerImpl::ObserveContentParentCreated(
    nsISupports* aContentParent) {
  // Do nothing; it's sufficient to get the PPPM.  But assign to nsRefPtr so we
  // don't leak the already_AddRefed object.
  RefPtr<ContentParent> cp = do_QueryObject(aContentParent);
  RefPtr<ParticularProcessPriorityManager> pppm =
      GetParticularProcessPriorityManager(cp);
}

void ProcessPriorityManagerImpl::ObserveContentParentDestroyed(
    nsISupports* aSubject) {
  nsCOMPtr<nsIPropertyBag2> props = do_QueryInterface(aSubject);
  NS_ENSURE_TRUE_VOID(props);

  uint64_t childID = CONTENT_PROCESS_ID_UNKNOWN;
  props->GetPropertyAsUint64(NS_LITERAL_STRING("childID"), &childID);
  NS_ENSURE_TRUE_VOID(childID != CONTENT_PROCESS_ID_UNKNOWN);

  if (auto entry = mParticularManagers.Lookup(childID)) {
    entry.Data()->ShutDown();
    mHighPriorityChildIDs.RemoveEntry(childID);
    entry.Remove();
  }
}

void ProcessPriorityManagerImpl::NotifyProcessPriorityChanged(
    ParticularProcessPriorityManager* aParticularManager,
    ProcessPriority aOldPriority) {
  ProcessPriority newPriority = aParticularManager->CurrentPriority();

  if (newPriority >= PROCESS_PRIORITY_FOREGROUND_HIGH &&
      aOldPriority < PROCESS_PRIORITY_FOREGROUND_HIGH) {
    mHighPriorityChildIDs.PutEntry(aParticularManager->ChildID());
  } else if (newPriority < PROCESS_PRIORITY_FOREGROUND_HIGH &&
             aOldPriority >= PROCESS_PRIORITY_FOREGROUND_HIGH) {
    mHighPriorityChildIDs.RemoveEntry(aParticularManager->ChildID());
  }
}

void ProcessPriorityManagerImpl::TabActivityChanged(
    BrowserParent* aBrowserParent, bool aIsActive) {
  RefPtr<ParticularProcessPriorityManager> pppm =
      GetParticularProcessPriorityManager(aBrowserParent->Manager());
  if (!pppm) {
    return;
  }

  Telemetry::ScalarAdd(
      Telemetry::ScalarID::DOM_CONTENTPROCESS_OS_PRIORITY_CHANGE_CONSIDERED, 1);

  pppm->TabActivityChanged(aBrowserParent, aIsActive);
}

NS_IMPL_ISUPPORTS(ParticularProcessPriorityManager, nsIObserver,
                  nsITimerCallback, nsISupportsWeakReference, nsINamed);

ParticularProcessPriorityManager::ParticularProcessPriorityManager(
    ContentParent* aContentParent)
    : mContentParent(aContentParent),
      mChildID(aContentParent->ChildID()),
      mPriority(PROCESS_PRIORITY_UNKNOWN),
      mHoldsCPUWakeLock(false),
      mHoldsHighPriorityWakeLock(false),
      mHoldsPlayingAudioWakeLock(false),
      mHoldsPlayingVideoWakeLock(false) {
  MOZ_ASSERT(XRE_IsParentProcess());
  LOGP("Creating ParticularProcessPriorityManager.");
}

void ParticularProcessPriorityManager::Init() {
  RegisterWakeLockObserver(this);

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->AddObserver(this, "remote-browser-shown", /* ownsWeak */ true);
    os->AddObserver(this, "ipc:browser-destroyed", /* ownsWeak */ true);
  }

  // This process may already hold the CPU lock; for example, our parent may
  // have acquired it on our behalf.
  mHoldsCPUWakeLock = IsHoldingWakeLock(NS_LITERAL_STRING("cpu"));
  mHoldsHighPriorityWakeLock =
      IsHoldingWakeLock(NS_LITERAL_STRING("high-priority"));
  mHoldsPlayingAudioWakeLock =
      IsHoldingWakeLock(NS_LITERAL_STRING("audio-playing"));
  mHoldsPlayingVideoWakeLock =
      IsHoldingWakeLock(NS_LITERAL_STRING("video-playing"));

  LOGP(
      "Done starting up.  mHoldsCPUWakeLock=%d, "
      "mHoldsHighPriorityWakeLock=%d, mHoldsPlayingAudioWakeLock=%d, "
      "mHoldsPlayingVideoWakeLock=%d",
      mHoldsCPUWakeLock, mHoldsHighPriorityWakeLock, mHoldsPlayingAudioWakeLock,
      mHoldsPlayingVideoWakeLock);
}

bool ParticularProcessPriorityManager::IsHoldingWakeLock(
    const nsAString& aTopic) {
  WakeLockInformation info;
  GetWakeLockInfo(aTopic, &info);
  return info.lockingProcesses().Contains(ChildID());
}

ParticularProcessPriorityManager::~ParticularProcessPriorityManager() {
  LOGP("Destroying ParticularProcessPriorityManager.");

  // Unregister our wake lock observer if ShutDown hasn't been called.  (The
  // wake lock observer takes raw refs, so we don't want to take chances here!)
  // We don't call UnregisterWakeLockObserver unconditionally because the code
  // will print a warning if it's called unnecessarily.

  if (mContentParent) {
    UnregisterWakeLockObserver(this);
  }
}

/* virtual */
void ParticularProcessPriorityManager::Notify(
    const WakeLockInformation& aInfo) {
  if (!mContentParent) {
    // We've been shut down.
    return;
  }

  bool* dest = nullptr;
  if (aInfo.topic().EqualsLiteral("cpu")) {
    dest = &mHoldsCPUWakeLock;
  } else if (aInfo.topic().EqualsLiteral("high-priority")) {
    dest = &mHoldsHighPriorityWakeLock;
  } else if (aInfo.topic().EqualsLiteral("audio-playing")) {
    dest = &mHoldsPlayingAudioWakeLock;
  } else if (aInfo.topic().EqualsLiteral("video-playing")) {
    dest = &mHoldsPlayingVideoWakeLock;
  }

  if (dest) {
    bool thisProcessLocks = aInfo.lockingProcesses().Contains(ChildID());
    if (thisProcessLocks != *dest) {
      *dest = thisProcessLocks;
      LOGP(
          "Got wake lock changed event. "
          "Now mHoldsCPUWakeLock=%d, mHoldsHighPriorityWakeLock=%d, "
          "mHoldsPlayingAudioWakeLock=%d, mHoldsPlayingVideoWakeLock=%d",
          mHoldsCPUWakeLock, mHoldsHighPriorityWakeLock,
          mHoldsPlayingAudioWakeLock, mHoldsPlayingVideoWakeLock);
      ResetPriority();
    }
  }
}

NS_IMETHODIMP
ParticularProcessPriorityManager::Observe(nsISupports* aSubject,
                                          const char* aTopic,
                                          const char16_t* aData) {
  if (!mContentParent) {
    // We've been shut down.
    return NS_OK;
  }

  nsDependentCString topic(aTopic);

  if (topic.EqualsLiteral("remote-browser-shown")) {
    OnRemoteBrowserFrameShown(aSubject);
  } else if (topic.EqualsLiteral("ipc:browser-destroyed")) {
    OnBrowserParentDestroyed(aSubject);
  } else {
    MOZ_ASSERT(false);
  }

  return NS_OK;
}

uint64_t ParticularProcessPriorityManager::ChildID() const {
  // We have to cache mContentParent->ChildID() instead of getting it from the
  // ContentParent each time because after ShutDown() is called, mContentParent
  // is null.  If we didn't cache ChildID(), then we wouldn't be able to run
  // LOGP() after ShutDown().
  return mChildID;
}

int32_t ParticularProcessPriorityManager::Pid() const {
  return mContentParent ? mContentParent->Pid() : -1;
}

const nsAutoCString& ParticularProcessPriorityManager::NameWithComma() {
  mNameWithComma.Truncate();
  if (!mContentParent) {
    return mNameWithComma;  // empty string
  }

  nsAutoString name;
  mContentParent->FriendlyName(name);
  if (name.IsEmpty()) {
    return mNameWithComma;  // empty string
  }

  CopyUTF16toUTF8(name, mNameWithComma);
  mNameWithComma.AppendLiteral(", ");
  return mNameWithComma;
}

void ParticularProcessPriorityManager::OnRemoteBrowserFrameShown(
    nsISupports* aSubject) {
  RefPtr<nsFrameLoader> fl = do_QueryObject(aSubject);
  NS_ENSURE_TRUE_VOID(fl);

  BrowserParent* tp = BrowserParent::GetFrom(fl);
  NS_ENSURE_TRUE_VOID(tp);

  MOZ_ASSERT(XRE_IsParentProcess());
  if (tp->Manager() != mContentParent) {
    return;
  }

  // Ignore notifications that aren't from a Browser
  if (fl->OwnerIsMozBrowserFrame()) {
    ResetPriority();
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (os) {
    os->RemoveObserver(this, "remote-browser-shown");
  }
}

void ParticularProcessPriorityManager::OnBrowserParentDestroyed(
    nsISupports* aSubject) {
  nsCOMPtr<nsIRemoteTab> tp = do_QueryInterface(aSubject);
  NS_ENSURE_TRUE_VOID(tp);

  MOZ_ASSERT(XRE_IsParentProcess());
  if (BrowserParent::GetFrom(tp)->Manager() != mContentParent) {
    return;
  }

  uint64_t tabId;
  if (NS_WARN_IF(NS_FAILED(tp->GetTabId(&tabId)))) {
    return;
  }

  mActiveBrowserParents.RemoveEntry(tabId);

  ResetPriority();
}

void ParticularProcessPriorityManager::ResetPriority() {
  ProcessPriority processPriority = ComputePriority();
  if (mPriority == PROCESS_PRIORITY_UNKNOWN || mPriority > processPriority) {
    // Apps set at a perceivable background priority are often playing media.
    // Most media will have short gaps while changing tracks between songs,
    // switching videos, etc.  Give these apps a longer grace period so they
    // can get their next track started, if there is one, before getting
    // downgraded.
    if (mPriority == PROCESS_PRIORITY_BACKGROUND_PERCEIVABLE) {
      ScheduleResetPriority(BACKGROUND_PERCEIVABLE_GRACE_PERIOD);
    } else {
      ScheduleResetPriority(BACKGROUND_GRACE_PERIOD);
    }
    return;
  }

  SetPriorityNow(processPriority);
}

void ParticularProcessPriorityManager::ResetPriorityNow() {
  SetPriorityNow(ComputePriority());
}

void ParticularProcessPriorityManager::ScheduleResetPriority(
    TimeoutPref aTimeoutPref) {
  if (mResetPriorityTimer) {
    LOGP("ScheduleResetPriority bailing; the timer is already running.");
    return;
  }

  uint32_t timeout = 0;
  switch (aTimeoutPref) {
    case BACKGROUND_PERCEIVABLE_GRACE_PERIOD:
      timeout = StaticPrefs::
          dom_ipc_processPriorityManager_backgroundPerceivableGracePeriodMS();
      break;
    case BACKGROUND_GRACE_PERIOD:
      timeout =
          StaticPrefs::dom_ipc_processPriorityManager_backgroundGracePeriodMS();
      break;
    default:
      MOZ_ASSERT(false, "Unrecognized timeout pref");
      break;
  }

  LOGP("Scheduling reset timer to fire in %dms.", timeout);
  NS_NewTimerWithCallback(getter_AddRefs(mResetPriorityTimer), this, timeout,
                          nsITimer::TYPE_ONE_SHOT);
}

NS_IMETHODIMP
ParticularProcessPriorityManager::Notify(nsITimer* aTimer) {
  LOGP("Reset priority timer callback; about to ResetPriorityNow.");
  ResetPriorityNow();
  mResetPriorityTimer = nullptr;
  return NS_OK;
}

ProcessPriority ParticularProcessPriorityManager::CurrentPriority() {
  return mPriority;
}

ProcessPriority ParticularProcessPriorityManager::ComputePriority() {
  if (!mActiveBrowserParents.IsEmpty() ||
      mContentParent->GetRemoteType().EqualsLiteral(EXTENSION_REMOTE_TYPE) ||
      mHoldsPlayingAudioWakeLock) {
    return PROCESS_PRIORITY_FOREGROUND;
  }

  if (mHoldsCPUWakeLock || mHoldsHighPriorityWakeLock ||
      mHoldsPlayingVideoWakeLock) {
    return PROCESS_PRIORITY_BACKGROUND_PERCEIVABLE;
  }

  return PROCESS_PRIORITY_BACKGROUND;
}

void ParticularProcessPriorityManager::SetPriorityNow(
    ProcessPriority aPriority) {
  if (aPriority == PROCESS_PRIORITY_UNKNOWN) {
    MOZ_ASSERT(false);
    return;
  }

  if (!ProcessPriorityManagerImpl::PrefsEnabled() || !mContentParent ||
      mPriority == aPriority) {
    return;
  }

  if (mPriority == aPriority) {
    hal::SetProcessPriority(Pid(), mPriority);
    return;
  }

  LOGP("Changing priority from %s to %s.", ProcessPriorityToString(mPriority),
       ProcessPriorityToString(aPriority));

  ProcessPriority oldPriority = mPriority;

  mPriority = aPriority;

  // We skip incrementing the DOM_CONTENTPROCESS_OS_PRIORITY_RAISED if we're
  // transitioning from the PROCESS_PRIORITY_UNKNOWN level, which is where
  // we initialize at.
  if (oldPriority < mPriority && oldPriority != PROCESS_PRIORITY_UNKNOWN) {
    Telemetry::ScalarAdd(
        Telemetry::ScalarID::DOM_CONTENTPROCESS_OS_PRIORITY_RAISED, 1);
  } else if (oldPriority > mPriority) {
    Telemetry::ScalarAdd(
        Telemetry::ScalarID::DOM_CONTENTPROCESS_OS_PRIORITY_LOWERED, 1);
  }

  hal::SetProcessPriority(Pid(), mPriority);

  if (oldPriority != mPriority) {
    ProcessPriorityManagerImpl::GetSingleton()->NotifyProcessPriorityChanged(
        this, oldPriority);

    Unused << mContentParent->SendNotifyProcessPriorityChanged(mPriority);
  }

  FireTestOnlyObserverNotification("process-priority-set",
                                   ProcessPriorityToString(mPriority));
}

void ParticularProcessPriorityManager::TabActivityChanged(
    BrowserParent* aBrowserParent, bool aIsActive) {
  MOZ_ASSERT(aBrowserParent);

  if (!aIsActive) {
    mActiveBrowserParents.RemoveEntry(aBrowserParent->GetTabId());
  } else {
    mActiveBrowserParents.PutEntry(aBrowserParent->GetTabId());
  }

  ResetPriority();
}

void ParticularProcessPriorityManager::ShutDown() {
  MOZ_ASSERT(mContentParent);

  UnregisterWakeLockObserver(this);

  if (mResetPriorityTimer) {
    mResetPriorityTimer->Cancel();
    mResetPriorityTimer = nullptr;
  }

  mContentParent = nullptr;
}

void ProcessPriorityManagerImpl::FireTestOnlyObserverNotification(
    const char* aTopic, const nsACString& aData /* = EmptyCString() */) {
  if (!TestMode()) {
    return;
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(os);

  nsPrintfCString topic("process-priority-manager:TEST-ONLY:%s", aTopic);

  LOG("Notifying observer %s, data %s", topic.get(),
      PromiseFlatCString(aData).get());
  os->NotifyObservers(nullptr, topic.get(), NS_ConvertUTF8toUTF16(aData).get());
}

void ParticularProcessPriorityManager::FireTestOnlyObserverNotification(
    const char* aTopic, const char* aData /* = nullptr */) {
  if (!ProcessPriorityManagerImpl::TestMode()) {
    return;
  }

  nsAutoCString data;
  if (aData) {
    data.AppendASCII(aData);
  }

  FireTestOnlyObserverNotification(aTopic, data);
}

void ParticularProcessPriorityManager::FireTestOnlyObserverNotification(
    const char* aTopic, const nsACString& aData /* = EmptyCString() */) {
  if (!ProcessPriorityManagerImpl::TestMode()) {
    return;
  }

  nsAutoCString data(nsPrintfCString("%" PRIu64, ChildID()));
  if (!aData.IsEmpty()) {
    data.Append(':');
    data.Append(aData);
  }

  // ProcessPriorityManagerImpl::GetSingleton() is guaranteed not to return
  // null, since ProcessPriorityManagerImpl is the only class which creates
  // ParticularProcessPriorityManagers.

  ProcessPriorityManagerImpl::GetSingleton()->FireTestOnlyObserverNotification(
      aTopic, data);
}

StaticRefPtr<ProcessPriorityManagerChild>
    ProcessPriorityManagerChild::sSingleton;

/* static */
void ProcessPriorityManagerChild::StaticInit() {
  if (!sSingleton) {
    sSingleton = new ProcessPriorityManagerChild();
    sSingleton->Init();
    ClearOnShutdown(&sSingleton);
  }
}

/* static */
ProcessPriorityManagerChild* ProcessPriorityManagerChild::Singleton() {
  StaticInit();
  return sSingleton;
}

NS_IMPL_ISUPPORTS(ProcessPriorityManagerChild, nsIObserver)

ProcessPriorityManagerChild::ProcessPriorityManagerChild() {
  if (XRE_IsParentProcess()) {
    mCachedPriority = PROCESS_PRIORITY_MASTER;
  } else {
    mCachedPriority = PROCESS_PRIORITY_UNKNOWN;
  }
}

void ProcessPriorityManagerChild::Init() {
  // The process priority should only be changed in child processes; don't even
  // bother listening for changes if we're in the main process.
  if (!XRE_IsParentProcess()) {
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    NS_ENSURE_TRUE_VOID(os);
    os->AddObserver(this, "ipc:process-priority-changed", /* weak = */ false);
  }
}

NS_IMETHODIMP
ProcessPriorityManagerChild::Observe(nsISupports* aSubject, const char* aTopic,
                                     const char16_t* aData) {
  MOZ_ASSERT(!strcmp(aTopic, "ipc:process-priority-changed"));

  nsCOMPtr<nsIPropertyBag2> props = do_QueryInterface(aSubject);
  NS_ENSURE_TRUE(props, NS_OK);

  int32_t priority = static_cast<int32_t>(PROCESS_PRIORITY_UNKNOWN);
  props->GetPropertyAsInt32(NS_LITERAL_STRING("priority"), &priority);
  NS_ENSURE_TRUE(ProcessPriority(priority) != PROCESS_PRIORITY_UNKNOWN, NS_OK);

  mCachedPriority = static_cast<ProcessPriority>(priority);

  return NS_OK;
}

bool ProcessPriorityManagerChild::CurrentProcessIsForeground() {
  return mCachedPriority == PROCESS_PRIORITY_UNKNOWN ||
         mCachedPriority >= PROCESS_PRIORITY_FOREGROUND;
}

}  // namespace

namespace mozilla {

/* static */
void ProcessPriorityManager::Init() {
  ProcessPriorityManagerImpl::StaticInit();
  ProcessPriorityManagerChild::StaticInit();
}

/* static */
void ProcessPriorityManager::SetProcessPriority(ContentParent* aContentParent,
                                                ProcessPriority aPriority) {
  MOZ_ASSERT(aContentParent);

  ProcessPriorityManagerImpl* singleton =
      ProcessPriorityManagerImpl::GetSingleton();
  if (singleton) {
    singleton->SetProcessPriority(aContentParent, aPriority);
  }
}

/* static */
bool ProcessPriorityManager::CurrentProcessIsForeground() {
  return ProcessPriorityManagerChild::Singleton()->CurrentProcessIsForeground();
}

/* static */
void ProcessPriorityManager::TabActivityChanged(BrowserParent* aBrowserParent,
                                                bool aIsActive) {
  MOZ_ASSERT(aBrowserParent);

  ProcessPriorityManagerImpl* singleton =
      ProcessPriorityManagerImpl::GetSingleton();
  if (!singleton) {
    return;
  }

  singleton->TabActivityChanged(aBrowserParent, aIsActive);
}

}  // namespace mozilla
