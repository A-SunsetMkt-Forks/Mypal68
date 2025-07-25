/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

// @flow

import { addThreadEventListeners, attachAllTargets } from "./events";
import { features } from "../../utils/prefs";
import { sameOrigin } from "../../utils/url";
import type { DevToolsClient, Target } from "./types";

// $FlowIgnore
const { defaultThreadOptions } = require("devtools/client/shared/thread-utils");

type Args = {
  currentTarget: Target,
  devToolsClient: DevToolsClient,
  targets: { [string]: Target },
  options: Object,
};

async function attachTargets(targetLists, args) {
  const { targets } = args;

  targetLists = targetLists.filter(target => !!target);

  for (const actor of Object.keys(targets)) {
    if (!targetLists.some(target => target.targetForm.threadActor == actor)) {
      delete targets[actor];
    }
  }

  for (const targetFront of targetLists) {
    try {
      await targetFront.attach();

      const threadActorID = targetFront.targetForm.threadActor;
      if (targets[threadActorID]) {
        continue;
      }
      targets[threadActorID] = targetFront;

      // Content process targets have already been attached by the toolbox.
      // And the thread front has been initialized from there.
      // So we only need to retrieve it here.
      let { threadFront } = targetFront;

      // But workers targets are still only managed by the debugger codebase
      // and so we have to attach their thread actor
      if (!threadFront) {
        [, threadFront] = await targetFront.attachThread({
          ...defaultThreadOptions(),
          ...args.options,
        });
        // NOTE: resume is not necessary for ProcessDescriptors and can be removed
        // once we switch to WorkerDescriptors
        threadFront.resume();
      }

      addThreadEventListeners(threadFront);
    } catch (e) {
      // If any of the workers have terminated since the list command initiated
      // then we will get errors. Ignore these.
    }
  }
}

async function listWorkerTargets(args: Args) {
  const { currentTarget, devToolsClient } = args;
  if (!currentTarget.isBrowsingContext || currentTarget.isContentProcess) {
    return [];
  }

  let workers = [];
  let allWorkers;
  let serviceWorkerRegistrations = [];
  if (attachAllTargets(currentTarget)) {
    workers = await devToolsClient.mainRoot.listAllWorkerTargets();

    // subprocess workers are ignored because they take several seconds to
    // attach to when opening the browser toolbox. See bug 1594597.
    workers = workers.filter(({ url }) => !url.includes("subprocess_worker"));

    allWorkers = workers;

    const {
      registrations,
    } = await devToolsClient.mainRoot.listServiceWorkerRegistrations();
    serviceWorkerRegistrations = registrations;
  } else {
    workers = (await currentTarget.listWorkers()).workers;
    if (currentTarget.url && features.windowlessServiceWorkers) {
      const {
        registrations,
      } = await devToolsClient.mainRoot.listServiceWorkerRegistrations();
      serviceWorkerRegistrations = registrations.filter(front =>
        sameOrigin(front.url, currentTarget.url)
      );
    }
  }

  for (const front of serviceWorkerRegistrations) {
    const { activeWorker, waitingWorker, installingWorker } = front;
    await maybeMarkServiceWorker(activeWorker, "active");
    await maybeMarkServiceWorker(waitingWorker, "waiting");
    await maybeMarkServiceWorker(installingWorker, "installing");
  }

  async function maybeMarkServiceWorker(info, status) {
    if (!info) {
      return;
    }

    if (!allWorkers) {
      allWorkers = await devToolsClient.mainRoot.listAllWorkerTargets();
    }
    const worker = allWorkers.find(front => front && front.id == info.id);
    if (!worker) {
      return;
    }

    worker.debuggerServiceWorkerStatus = status;
    if (!workers.includes(worker)) {
      workers.push(worker);
    }
  }

  return workers;
}

async function listProcessTargets(args: Args) {
  const { currentTarget, devToolsClient } = args;
  if (!attachAllTargets(currentTarget)) {
    return [];
  }

  const { processes } = await devToolsClient.mainRoot.listProcesses();
  const targets = await Promise.all(
    processes
      .filter(descriptor => !descriptor.isParent)
      .map(descriptor => descriptor.getTarget())
  );

  return targets;
}

export async function updateTargets(args: Args) {
  const workers = await listWorkerTargets(args);
  const processes = await listProcessTargets(args);
  await attachTargets([...workers, ...processes], args);
}
