/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef ipc_glue_MessageChannel_h
#define ipc_glue_MessageChannel_h 1

#include "base/basictypes.h"
#include "base/message_loop.h"

#include "mozilla/Atomics.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Monitor.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Vector.h"
#if defined(OS_WIN)
#  include "mozilla/ipc/Neutering.h"
#endif  // defined(OS_WIN)
#include "mozilla/ipc/Transport.h"
#include "MessageLink.h"
#include "nsThreadUtils.h"

#include <deque>
#include <functional>
#include <map>
#include <math.h>
#include <stack>
#include <vector>

class nsIEventTarget;

namespace mozilla {
namespace ipc {

class MessageChannel;
class IToplevelProtocol;
class ActorLifecycleProxy;

enum class MessageDirection {
  eSending,
  eReceiving,
};

enum class MessagePhase {
  Endpoint,
  TransferStart,
  TransferEnd,
};

enum class SyncSendError {
  SendSuccess,
  PreviousTimeout,
  SendingCPOWWhileDispatchingSync,
  SendingCPOWWhileDispatchingUrgent,
  NotConnectedBeforeSend,
  DisconnectedDuringSend,
  CancelledBeforeSend,
  CancelledAfterSend,
  TimedOut,
  ReplyError,
};

enum class ResponseRejectReason {
  SendError,
  ChannelClosed,
  HandlerRejected,
  ActorDestroyed,
  EndGuard_,
};

template <typename T>
using ResolveCallback = std::function<void(T&&)>;

using RejectCallback = std::function<void(ResponseRejectReason)>;

enum ChannelState {
  ChannelClosed,
  ChannelOpening,
  ChannelConnected,
  ChannelTimeout,
  ChannelClosing,
  ChannelError
};

class AutoEnterTransaction;

class MessageChannel : HasResultCodes, MessageLoop::DestructionObserver {
  friend class ProcessLink;
  friend class ThreadLink;
#ifdef FUZZING
  friend class ProtocolFuzzerHelper;
#endif

  class CxxStackFrame;
  class InterruptFrame;

  typedef mozilla::Monitor Monitor;

  // We could templatize the actor type but it would unnecessarily
  // expand the code size. Using the actor address as the
  // identifier is already good enough.
  typedef void* ActorIdType;

 public:
  struct UntypedCallbackHolder {
    UntypedCallbackHolder(ActorIdType aActorId, RejectCallback&& aReject)
        : mActorId(aActorId), mReject(std::move(aReject)) {}

    virtual ~UntypedCallbackHolder() = default;

    void Reject(ResponseRejectReason&& aReason) { mReject(std::move(aReason)); }

    ActorIdType mActorId;
    RejectCallback mReject;
  };

  template <typename Value>
  struct CallbackHolder : public UntypedCallbackHolder {
    CallbackHolder(ActorIdType aActorId, ResolveCallback<Value>&& aResolve,
                   RejectCallback&& aReject)
        : UntypedCallbackHolder(aActorId, std::move(aReject)),
          mResolve(std::move(aResolve)) {}

    void Resolve(Value&& aReason) { mResolve(std::move(aReason)); }

    ResolveCallback<Value> mResolve;
  };

 private:
  static Atomic<size_t> gUnresolvedResponses;
  friend class PendingResponseReporter;

 public:
  static const int32_t kNoTimeout;

  typedef IPC::Message Message;
  typedef IPC::MessageInfo MessageInfo;
  typedef mozilla::ipc::Transport Transport;

  explicit MessageChannel(const char* aName, IToplevelProtocol* aListener);
  ~MessageChannel();

  IToplevelProtocol* Listener() const { return mListener; }

  // "Open" from the perspective of the transport layer; the underlying
  // socketpair/pipe should already be created.
  //
  // Returns true if the transport layer was successfully connected,
  // i.e., mChannelState == ChannelConnected.
  bool Open(UniquePtr<Transport> aTransport, MessageLoop* aIOLoop = 0,
            Side aSide = UnknownSide);

  // "Open" a connection to another thread in the same process.
  //
  // Returns true if the transport layer was successfully connected,
  // i.e., mChannelState == ChannelConnected.
  //
  // For more details on the process of opening a channel between
  // threads, see the extended comment on this function
  // in MessageChannel.cpp.
  bool Open(MessageChannel* aTargetChan, nsIEventTarget* aEventTarget,
            Side aSide);

  // "Open" a connection to an actor on the current thread.
  //
  // Returns true if the transport layer was successfully connected,
  // i.e., mChannelState == ChannelConnected.
  //
  // Same-thread channels may not perform synchronous or blocking message
  // sends, to avoid deadlocks.
  bool OpenOnSameThread(MessageChannel* aTargetChan, Side aSide);

  // Close the underlying transport channel.
  void Close();

  // Force the channel to behave as if a channel error occurred. Valid
  // for process links only, not thread links.
  void CloseWithError();

  void CloseWithTimeout();

  void SetAbortOnError(bool abort) { mAbortOnError = abort; }

  // Call aInvoke for each pending message until it returns false.
  // XXX: You must get permission from an IPC peer to use this function
  //      since it requires custom deserialization and re-orders events.
  void PeekMessages(const std::function<bool(const Message& aMsg)>& aInvoke);

  // Misc. behavioral traits consumers can request for this channel
  enum ChannelFlags {
    REQUIRE_DEFAULT = 0,
    // Windows: if this channel operates on the UI thread, indicates
    // WindowsMessageLoop code should enable deferred native message
    // handling to prevent deadlocks. Should only be used for protocols
    // that manage child processes which might create native UI, like
    // plugins.
    REQUIRE_DEFERRED_MESSAGE_PROTECTION = 1 << 0,
    // Windows: When this flag is specified, any wait that occurs during
    // synchronous IPC will be alertable, thus allowing a11y code in the
    // chrome process to reenter content while content is waiting on a
    // synchronous call.
    REQUIRE_A11Y_REENTRY = 1 << 1,
  };
  void SetChannelFlags(ChannelFlags aFlags) { mFlags = aFlags; }
  ChannelFlags GetChannelFlags() { return mFlags; }

  // Asynchronously send a message to the other side of the channel
  bool Send(UniquePtr<Message> aMsg);

  // Asynchronously send a message to the other side of the channel
  // and wait for asynchronous reply.
  template <typename Value>
  void Send(UniquePtr<Message> aMsg, ActorIdType aActorId,
            ResolveCallback<Value>&& aResolve, RejectCallback&& aReject) {
    int32_t seqno = NextSeqno();
    aMsg->set_seqno(seqno);
    if (!Send(std::move(aMsg))) {
      aReject(ResponseRejectReason::SendError);
      return;
    }

    UniquePtr<UntypedCallbackHolder> callback =
        MakeUnique<CallbackHolder<Value>>(aActorId, std::move(aResolve),
                                          std::move(aReject));
    mPendingResponses.insert(std::make_pair(seqno, std::move(callback)));
    gUnresolvedResponses++;
  }

  bool SendBuildIDsMatchMessage(const char* aParentBuildI);
  bool DoBuildIDsMatch() { return mBuildIDsConfirmedMatch; }

  // Synchronously send |msg| (i.e., wait for |reply|)
  bool Send(UniquePtr<Message> aMsg, Message* aReply);

  // Make an Interrupt call to the other side of the channel
  bool Call(UniquePtr<Message> aMsg, Message* aReply);

  // Wait until a message is received
  bool WaitForIncomingMessage();

  bool CanSend() const;

  // Remove and return a callback that needs reply
  UniquePtr<UntypedCallbackHolder> PopCallback(const Message& aMsg);

  // Used to reject and remove pending responses owned by the given
  // actor when it's about to be destroyed.
  void RejectPendingResponsesForActor(ActorIdType aActorId);

  // If sending a sync message returns an error, this function gives a more
  // descriptive error message.
  SyncSendError LastSendError() const {
    AssertWorkerThread();
    return mLastSendError;
  }

  // Currently only for debugging purposes, doesn't aquire mMonitor.
  ChannelState GetChannelState__TotallyRacy() const { return mChannelState; }

  void SetReplyTimeoutMs(int32_t aTimeoutMs);

  bool IsOnCxxStack() const { return !mCxxStackFrames.empty(); }

  bool IsInTransaction() const;
  void CancelCurrentTransaction();

  // Force all calls to Send to defer actually sending messages. This will
  // cause sync messages to block until another thread calls
  // StopPostponingSends.
  //
  // This must be called from the worker thread.
  void BeginPostponingSends();

  // Stop postponing sent messages, and immediately flush all postponed
  // messages to the link. This may be called from any thread.
  //
  // Note that there are no ordering guarantees between two different
  // MessageChannels. If channel B sends a message, then stops postponing
  // channel A, messages from A may arrive before B. The easiest way to order
  // this, if needed, is to make B send a sync message.
  void StopPostponingSends();

  /**
   * This function is used by hang annotation code to determine which IPDL
   * actor is highest in the call stack at the time of the hang. It should
   * be called from the main thread when a sync or intr message is about to
   * be sent.
   */
  int32_t GetTopmostMessageRoutingId() const;

  // Unsound_IsClosed and Unsound_NumQueuedMessages are safe to call from any
  // thread, but they make no guarantees about whether you'll get an
  // up-to-date value; the values are written on one thread and read without
  // locking, on potentially different threads.  Thus you should only use
  // them when you don't particularly care about getting a recent value (e.g.
  // in a memory report).
  bool Unsound_IsClosed() const {
    return mLink ? mLink->Unsound_IsClosed() : true;
  }
  uint32_t Unsound_NumQueuedMessages() const {
    return mLink ? mLink->Unsound_NumQueuedMessages() : 0;
  }

  static bool IsPumpingMessages() { return sIsPumpingMessages; }
  static void SetIsPumpingMessages(bool aIsPumping) {
    sIsPumpingMessages = aIsPumping;
  }

  /**
   * Does this MessageChannel cross process boundaries?
   */
  bool IsCrossProcess() const { return mIsCrossProcess; }

#ifdef OS_WIN
  struct MOZ_STACK_CLASS SyncStackFrame {
    SyncStackFrame(MessageChannel* channel, bool interrupt);
    ~SyncStackFrame();

    bool mInterrupt;
    bool mSpinNestedEvents;
    bool mListenerNotified;
    MessageChannel* mChannel;

    // The previous stack frame for this channel.
    SyncStackFrame* mPrev;

    // The previous stack frame on any channel.
    SyncStackFrame* mStaticPrev;
  };
  friend struct MessageChannel::SyncStackFrame;

  static bool IsSpinLoopActive() {
    for (SyncStackFrame* frame = sStaticTopFrame; frame; frame = frame->mPrev) {
      if (frame->mSpinNestedEvents) return true;
    }
    return false;
  }

 protected:
  // The deepest sync stack frame for this channel.
  SyncStackFrame* mTopFrame;

  bool mIsSyncWaitingOnNonMainThread;

  // The deepest sync stack frame on any channel.
  static SyncStackFrame* sStaticTopFrame;

 public:
  void ProcessNativeEventsInInterruptCall();
  static void NotifyGeckoEventDispatch();

 private:
  void SpinInternalEventLoop();
#  if defined(ACCESSIBILITY)
  bool WaitForSyncNotifyWithA11yReentry();
#  endif  // defined(ACCESSIBILITY)
#endif    // defined(OS_WIN)

 private:
  void CommonThreadOpenInit(MessageChannel* aTargetChan, Side aSide);
  void OnOpenAsSlave(MessageChannel* aTargetChan, Side aSide);

  void PostErrorNotifyTask();
  void OnNotifyMaybeChannelError();
  void ReportConnectionError(const char* aChannelName,
                             Message* aMsg = nullptr) const;
  void ReportMessageRouteError(const char* channelName) const;
  bool MaybeHandleError(Result code, const Message& aMsg,
                        const char* channelName);

  void Clear();

  // Send OnChannelConnected notification to listeners.
  void DispatchOnChannelConnected();

  bool InterruptEventOccurred();
  bool HasPendingEvents();

  void ProcessPendingRequests(AutoEnterTransaction& aTransaction);
  bool ProcessPendingRequest(Message&& aUrgent);

  void MaybeUndeferIncall();
  void EnqueuePendingMessages();

  // Dispatches an incoming message to its appropriate handler.
  void DispatchMessage(Message&& aMsg);

  // DispatchMessage will route to one of these functions depending on the
  // protocol type of the message.
  void DispatchSyncMessage(ActorLifecycleProxy* aProxy, const Message& aMsg,
                           Message*& aReply);
  void DispatchAsyncMessage(ActorLifecycleProxy* aProxy, const Message& aMsg);
  void DispatchInterruptMessage(ActorLifecycleProxy* aProxy, Message&& aMsg,
                                size_t aStackDepth);

  // Return true if the wait ended because a notification was received.
  //
  // Return false if the time elapsed from when we started the process of
  // waiting until afterwards exceeded the currently allotted timeout.
  // That *DOES NOT* mean false => "no event" (== timeout); there are many
  // circumstances that could cause the measured elapsed time to exceed the
  // timeout EVEN WHEN we were notified.
  //
  // So in sum: true is a meaningful return value; false isn't,
  // necessarily.
  bool WaitForSyncNotify(bool aHandleWindowsMessages);
  bool WaitForInterruptNotify();

  bool WaitResponse(bool aWaitTimedOut);

  bool ShouldContinueFromTimeout();

  void EndTimeout();
  void CancelTransaction(int transaction);

  void RepostAllMessages();

  // The "remote view of stack depth" can be different than the
  // actual stack depth when there are out-of-turn replies.  When we
  // receive one, our actual Interrupt stack depth doesn't decrease, but
  // the other side (that sent the reply) thinks it has.  So, the
  // "view" returned here is |stackDepth| minus the number of
  // out-of-turn replies.
  //
  // Only called from the worker thread.
  size_t RemoteViewOfStackDepth(size_t stackDepth) const {
    AssertWorkerThread();
    return stackDepth - mOutOfTurnReplies.size();
  }

  int32_t NextSeqno() {
    AssertWorkerThread();
    return (mSide == ChildSide) ? --mNextSeqno : ++mNextSeqno;
  }

  // This helper class manages mCxxStackDepth on behalf of MessageChannel.
  // When the stack depth is incremented from zero to non-zero, it invokes
  // a callback, and similarly for when the depth goes from non-zero to zero.
  void EnteredCxxStack();
  void ExitedCxxStack();

  void EnteredCall();
  void ExitedCall();

  void EnteredSyncSend();
  void ExitedSyncSend();

  void DebugAbort(const char* file, int line, const char* cond, const char* why,
                  bool reply = false);

  // This method is only safe to call on the worker thread, or in a
  // debugger with all threads paused.
  void DumpInterruptStack(const char* const pfx = "") const;

  void AddProfilerMarker(const IPC::Message& aMessage,
                         MessageDirection aDirection);

 private:
  // Called from both threads
  size_t InterruptStackDepth() const {
    mMonitor->AssertCurrentThreadOwns();
    return mInterruptStack.size();
  }

  bool AwaitingInterruptReply() const {
    mMonitor->AssertCurrentThreadOwns();
    return !mInterruptStack.empty();
  }
  bool AwaitingIncomingMessage() const {
    mMonitor->AssertCurrentThreadOwns();
    return mIsWaitingForIncoming;
  }

  class MOZ_STACK_CLASS AutoEnterWaitForIncoming {
   public:
    explicit AutoEnterWaitForIncoming(MessageChannel& aChannel)
        : mChannel(aChannel) {
      aChannel.mMonitor->AssertCurrentThreadOwns();
      aChannel.mIsWaitingForIncoming = true;
    }

    ~AutoEnterWaitForIncoming() { mChannel.mIsWaitingForIncoming = false; }

   private:
    MessageChannel& mChannel;
  };
  friend class AutoEnterWaitForIncoming;

  // Returns true if we're dispatching an async message's callback.
  bool DispatchingAsyncMessage() const {
    AssertWorkerThread();
    return mDispatchingAsyncMessage;
  }

  int DispatchingAsyncMessageNestedLevel() const {
    AssertWorkerThread();
    return mDispatchingAsyncMessageNestedLevel;
  }

  bool Connected() const;

 private:
  // Executed on the IO thread.
  void NotifyWorkerThread();

  // Return true if |aMsg| is a special message targeted at the IO
  // thread, in which case it shouldn't be delivered to the worker.
  bool MaybeInterceptSpecialIOMessage(const Message& aMsg);

  void OnChannelConnected(int32_t peer_id);

  // Tell the IO thread to close the channel and wait for it to ACK.
  void SynchronouslyClose();

  // Returns true if ShouldDeferMessage(aMsg) is guaranteed to return true.
  // Otherwise, the result of ShouldDeferMessage(aMsg) may be true or false,
  // depending on context.
  static bool IsAlwaysDeferred(const Message& aMsg);

  // Helper for sending a message via the link. This should only be used for
  // non-special messages that might have to be postponed.
  void SendMessageToLink(UniquePtr<Message> aMsg);

  bool WasTransactionCanceled(int transaction);
  bool ShouldDeferMessage(const Message& aMsg);
  bool ShouldDeferInterruptMessage(const Message& aMsg, size_t aStackDepth);
  void OnMessageReceivedFromLink(Message&& aMsg);
  void OnChannelErrorFromLink();

 private:
  // Run on the not current thread.
  void NotifyChannelClosed();
  void NotifyMaybeChannelError();

 private:
  // Can be run on either thread
  void AssertWorkerThread() const {
    MOZ_ASSERT(mWorkerThread, "Channel hasn't been opened yet");
    MOZ_RELEASE_ASSERT(mWorkerThread == PR_GetCurrentThread(),
                       "not on worker thread!");
  }

  // The "link" thread is either the I/O thread (ProcessLink), the other
  // actor's work thread (ThreadLink), or the worker thread (same-thread
  // channels).
  void AssertLinkThread() const {
    if (mIsSameThreadChannel) {
      // If we're a same-thread channel, we have to be on our worker
      // thread.
      AssertWorkerThread();
      return;
    }

    // If we aren't a same-thread channel, our "link" thread is _not_ our
    // worker thread!
    MOZ_ASSERT(mWorkerThread, "Channel hasn't been opened yet");
    MOZ_RELEASE_ASSERT(mWorkerThread != PR_GetCurrentThread(),
                       "on worker thread but should not be!");
  }

 private:
  class MessageTask : public CancelableRunnable,
                      public LinkedListElement<RefPtr<MessageTask>>,
                      public nsIRunnablePriority {
   public:
    explicit MessageTask(MessageChannel* aChannel, Message&& aMessage);

    NS_DECL_ISUPPORTS_INHERITED

    NS_IMETHOD Run() override;
    nsresult Cancel() override;
    NS_IMETHOD GetPriority(uint32_t* aPriority) override;
    void Post();
    void Clear();

    bool IsScheduled() const { return mScheduled; }

    Message& Msg() { return mMessage; }
    const Message& Msg() const { return mMessage; }

   private:
    MessageTask() = delete;
    MessageTask(const MessageTask&) = delete;
    ~MessageTask() = default;

    MessageChannel* mChannel;
    Message mMessage;
    bool mScheduled : 1;
  };

  bool ShouldRunMessage(const Message& aMsg);
  void RunMessage(MessageTask& aTask);

  typedef LinkedList<RefPtr<MessageTask>> MessageQueue;
  typedef std::map<size_t, Message> MessageMap;
  typedef std::map<size_t, UniquePtr<UntypedCallbackHolder>> CallbackMap;
  typedef IPC::Message::msgid_t msgid_t;

  void WillDestroyCurrentMessageLoop() override;

 private:
  // This will be a string literal, so lifetime is not an issue.
  const char* mName;

  // Based on presumption the listener owns and overlives the channel,
  // this is never nullified.
  IToplevelProtocol* mListener;
  ChannelState mChannelState;
  Monitor2* mMonitor;
  Side mSide;
  bool mIsCrossProcess;
  UniquePtr<MessageLink> mLink;
  MessageLoop* mWorkerLoop;  // thread where work is done
  RefPtr<CancelableRunnable>
      mChannelErrorTask;  // NotifyMaybeChannelError runnable

  // Thread we are allowed to send and receive on. This persists even after
  // mWorkerLoop is cleared during channel shutdown.
  PRThread* mWorkerThread;

  // Timeout periods are broken up in two to prevent system suspension from
  // triggering an abort. This method (called by WaitForEvent with a 'did
  // timeout' flag) decides if we should wait again for half of mTimeoutMs
  // or give up.
  int32_t mTimeoutMs;
  bool mInTimeoutSecondHalf;

  // Worker-thread only; sequence numbers for messages that require
  // replies.
  int32_t mNextSeqno;

  static bool sIsPumpingMessages;

  // If ::Send returns false, this gives a more descriptive error.
  SyncSendError mLastSendError;

  template <class T>
  class AutoSetValue {
   public:
    explicit AutoSetValue(T& var, const T& newValue)
        : mVar(var), mPrev(var), mNew(newValue) {
      mVar = newValue;
    }
    ~AutoSetValue() {
      // The value may have been zeroed if the transaction was
      // canceled. In that case we shouldn't return it to its previous
      // value.
      if (mVar == mNew) {
        mVar = mPrev;
      }
    }

   private:
    T& mVar;
    T mPrev;
    T mNew;
  };

  bool mDispatchingAsyncMessage;
  int mDispatchingAsyncMessageNestedLevel;

  // When we send an urgent request from the parent process, we could race
  // with an RPC message that was issued by the child beforehand. In this
  // case, if the parent were to wake up while waiting for the urgent reply,
  // and process the RPC, it could send an additional urgent message. The
  // child would wake up to process the urgent message (as it always will),
  // then send a reply, which could be received by the parent out-of-order
  // with respect to the first urgent reply.
  //
  // To address this problem, urgent or RPC requests are associated with a
  // "transaction". Whenever one side of the channel wishes to start a
  // chain of RPC/urgent messages, it allocates a new transaction ID. Any
  // messages the parent receives, not apart of this transaction, are
  // deferred. When issuing RPC/urgent requests on top of a started
  // transaction, the initiating transaction ID is used.
  //
  // To ensure IDs are unique, we use sequence numbers for transaction IDs,
  // which grow in opposite directions from child to parent.

  friend class AutoEnterTransaction;
  AutoEnterTransaction* mTransactionStack;

  int32_t CurrentNestedInsideSyncTransaction() const;

  bool AwaitingSyncReply() const;
  int AwaitingSyncReplyNestedLevel() const;

  bool DispatchingSyncMessage() const;
  int DispatchingSyncMessageNestedLevel() const;

#ifdef DEBUG
  void AssertMaybeDeferredCountCorrect();
#else
  void AssertMaybeDeferredCountCorrect() {}
#endif

  // If a sync message times out, we store its sequence number here. Any
  // future sync messages will fail immediately. Once the reply for original
  // sync message is received, we allow sync messages again.
  //
  // When a message times out, nothing is done to inform the other side. The
  // other side will eventually dispatch the message and send a reply. Our
  // side is responsible for replying to all sync messages sent by the other
  // side when it dispatches the timed out message. The response is always an
  // error.
  //
  // A message is only timed out if it initiated a transaction. This avoids
  // hitting a lot of corner cases with message nesting that we don't really
  // care about.
  int32_t mTimedOutMessageSeqno;
  int mTimedOutMessageNestedLevel;

  // Queue of all incoming messages.
  //
  // If both this side and the other side are functioning correctly, the queue
  // can only be in certain configurations.  Let
  //
  //   |A<| be an async in-message,
  //   |S<| be a sync in-message,
  //   |C<| be an Interrupt in-call,
  //   |R<| be an Interrupt reply.
  //
  // The queue can only match this configuration
  //
  //  A<* (S< | C< | R< (?{mInterruptStack.size() == 1} A<* (S< | C<)))
  //
  // The other side can send as many async messages |A<*| as it wants before
  // sending us a blocking message.
  //
  // The first case is |S<|, a sync in-msg.  The other side must be blocked,
  // and thus can't send us any more messages until we process the sync
  // in-msg.
  //
  // The second case is |C<|, an Interrupt in-call; the other side must be
  // blocked. (There's a subtlety here: this in-call might have raced with an
  // out-call, but we detect that with the mechanism below,
  // |mRemoteStackDepth|, and races don't matter to the queue.)
  //
  // Final case, the other side replied to our most recent out-call |R<|.
  // If that was the *only* out-call on our stack,
  // |?{mInterruptStack.size() == 1}|, then other side "finished with us,"
  // and went back to its own business.  That business might have included
  // sending any number of async message |A<*| until sending a blocking
  // message |(S< | C<)|.  If we had more than one Interrupt call on our
  // stack, the other side *better* not have sent us another blocking
  // message, because it's blocked on a reply from us.
  //
  MessageQueue mPending;

  // The number of messages in mPending for which IsAlwaysDeferred is false
  // (i.e., the number of messages that might not be deferred, depending on
  // context).
  size_t mMaybeDeferredPendingCount;

  // Stack of all the out-calls on which this channel is awaiting responses.
  // Each stack refers to a different protocol and the stacks are mutually
  // exclusive: multiple outcalls of the same kind cannot be initiated while
  // another is active.
  std::stack<MessageInfo> mInterruptStack;

  // This is what we think the Interrupt stack depth is on the "other side" of
  // this Interrupt channel.  We maintain this variable so that we can detect
  // racy Interrupt calls.  With each Interrupt out-call sent, we send along
  // what *we* think the stack depth of the remote side is *before* it will
  // receive the Interrupt call.
  //
  // After sending the out-call, our stack depth is "incremented" by pushing
  // that pending message onto mPending.
  //
  // Then when processing an in-call |c|, it must be true that
  //
  //   mInterruptStack.size() == c.remoteDepth
  //
  // I.e., my depth is actually the same as what the other side thought it
  // was when it sent in-call |c|.  If this fails to hold, we have detected
  // racy Interrupt calls.
  //
  // We then increment mRemoteStackDepth *just before* processing the
  // in-call, since we know the other side is waiting on it, and decrement
  // it *just after* finishing processing that in-call, since our response
  // will pop the top of the other side's |mPending|.
  //
  // One nice aspect of this race detection is that it is symmetric; if one
  // side detects a race, then the other side must also detect the same race.
  size_t mRemoteStackDepthGuess;

  // Approximation of code frames on the C++ stack. It can only be
  // interpreted as the implication:
  //
  //  !mCxxStackFrames.empty() => MessageChannel code on C++ stack
  //
  // This member is only accessed on the worker thread, and so is not
  // protected by mMonitor.  It is managed exclusively by the helper
  // |class CxxStackFrame|.
  mozilla::Vector<InterruptFrame> mCxxStackFrames;

  // Did we process an Interrupt out-call during this stack?  Only meaningful in
  // ExitedCxxStack(), from which this variable is reset.
  bool mSawInterruptOutMsg;

  // Are we waiting on this channel for an incoming message? This is used
  // to implement WaitForIncomingMessage(). Must only be accessed while owning
  // mMonitor.
  bool mIsWaitingForIncoming;

  // Map of replies received "out of turn", because of Interrupt
  // in-calls racing with replies to outstanding in-calls.  See
  // https://bugzilla.mozilla.org/show_bug.cgi?id=521929.
  MessageMap mOutOfTurnReplies;

  // Map of async Callbacks that are still waiting replies.
  CallbackMap mPendingResponses;

  // Stack of Interrupt in-calls that were deferred because of race
  // conditions.
  std::stack<Message> mDeferred;

#ifdef OS_WIN
  HANDLE mEvent;
#endif

  // Should the channel abort the process from the I/O thread when
  // a channel error occurs?
  bool mAbortOnError;

  // True if the listener has already been notified of a channel close or
  // error.
  bool mNotifiedChannelDone;

  // See SetChannelFlags
  ChannelFlags mFlags;

  // Task and state used to asynchronously notify channel has been connected
  // safely.  This is necessary to be able to cancel notification if we are
  // closed at the same time.
  RefPtr<CancelableRunnable> mOnChannelConnectedTask;
  bool mPeerPidSet;
  int32_t mPeerPid;

  // Channels can enter messages are not sent immediately; instead, they are
  // held in a queue until another thread deems it is safe to send them.
  bool mIsPostponingSends;
  std::vector<UniquePtr<Message>> mPostponedSends;

  bool mBuildIDsConfirmedMatch;

  // If this is true, both ends of this message channel have event targets
  // on the same thread.
  bool mIsSameThreadChannel;
};

void CancelCPOWs();

}  // namespace ipc
}  // namespace mozilla

namespace IPC {
template <>
struct ParamTraits<mozilla::ipc::ResponseRejectReason>
    : public ContiguousEnumSerializer<
          mozilla::ipc::ResponseRejectReason,
          mozilla::ipc::ResponseRejectReason::SendError,
          mozilla::ipc::ResponseRejectReason::EndGuard_> {};
}  // namespace IPC

#endif  // ifndef ipc_glue_MessageChannel_h
