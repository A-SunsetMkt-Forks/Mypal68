# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/

# ALL CHANGES TO THIS FILE MUST HAVE REVIEW FROM A MARIONETTE PEER!
#
# The Marionette Python client is used out-of-tree with various builds of
# Firefox. Removing a preference from this file will cause regressions,
# so please be careful and get review from a Testing :: Marionette peer
# before you make any changes to this file.

from __future__ import absolute_import

import os
import sys
import tempfile
import time
import traceback

from copy import deepcopy

import mozversion

from mozprofile import Profile
from mozrunner import Runner, FennecEmulatorRunner
from six import reraise

from . import errors


class GeckoInstance(object):
    required_prefs = {
        # Increase the APZ content response timeout in tests to 1 minute.
        # This is to accommodate the fact that test environments tends to be slower
        # than production environments (with the b2g emulator being the slowest of them
        # all), resulting in the production timeout value sometimes being exceeded
        # and causing false-positive test failures. See bug 1176798, bug 1177018,
        # bug 1210465.
        "apz.content_response_timeout": 60000,

        # Do not send Firefox health reports to the production server
        # removed in Firefox 59
        "datareporting.healthreport.about.reportUrl": "http://%(server)s/dummy/abouthealthreport/",
        "datareporting.healthreport.documentServerURI": "http://%(server)s/dummy/healthreport/",

        # Do not show datareporting policy notifications which can interfer with tests
        "datareporting.policy.dataSubmissionPolicyBypassNotification": True,

        # Automatically unload beforeunload alerts
        "dom.disable_beforeunload": True,

        # Disable the ProcessHangMonitor
        "dom.ipc.reportProcessHangs": False,

        # No slow script dialogs
        "dom.max_chrome_script_run_time": 0,
        "dom.max_script_run_time": 0,

        # Only load extensions from the application and user profile
        # AddonManager.SCOPE_PROFILE + AddonManager.SCOPE_APPLICATION
        "extensions.autoDisableScopes": 0,
        "extensions.enabledScopes": 5,
        # Disable intalling any distribution add-ons
        "extensions.installDistroAddons": False,
        # Make sure Shield doesn't hit the network.
        # Removed in Firefox 60.
        "extensions.shield-recipe-client.api_url": "",
        # Disable extensions compatibility dialogue.
        # Removed in Firefox 61.
        "extensions.showMismatchUI": False,
        # Turn off extension updates so they don't bother tests
        "extensions.update.enabled": False,
        "extensions.update.notifyUser": False,

        # Allow the application to have focus even it runs in the background
        "focusmanager.testmode": True,

        # Disable useragent updates
        "general.useragent.updates.enabled": False,

        # Always use network provider for geolocation tests
        # so we bypass the OSX dialog raised by the corelocation provider
        "geo.provider.testing": True,
        # Do not scan Wifi
        "geo.wifi.scan": False,


        # Enable Marionette component
        "marionette.enabled": True,
        # (deprecated and can be removed when Firefox 60 ships)
        "marionette.defaultPrefs.enabled": True,

        # Disable recommended automation prefs in CI
        "marionette.prefs.recommended": False,

        # Disable download and usage of OpenH264, and Widevine plugins
        "media.gmp-manager.updateEnabled": False,

        "media.volume_scale": "0.01",

        # Do not prompt for temporary redirects
        "network.http.prompt-temp-redirect": False,
        # Disable speculative connections so they aren"t reported as leaking when they"re
        # hanging around
        "network.http.speculative-parallel-limit": 0,
        # Do not automatically switch between offline and online
        "network.manage-offline-status": False,
        # Make sure SNTP requests don't hit the network
        "network.sntp.pools": "%(server)s",

        # Don't do network connections for mitm priming
        "security.certerrors.mitm.priming.enabled": False,

        # Tests don't wait for the notification button security delay
        "security.notification_enable_delay": 0,

        # Disable password capture, so that tests that include forms aren"t
        # influenced by the presence of the persistent doorhanger notification
        "signon.rememberSignons": False,

        # Prevent starting into safe mode after application crashes
        "toolkit.startup.max_resumed_crashes": -1,

        # We want to collect telemetry, but we don't want to send in the results
        "toolkit.telemetry.server": "https://%(server)s/dummy/telemetry/",

        # Enabling the support for File object creation in the content process.
        "dom.file.createInChild": True,
    }

    def __init__(self, host=None, port=None, bin=None, profile=None, addons=None,
                 app_args=None, symbols_path=None, gecko_log=None, prefs=None,
                 workspace=None, verbose=0, headless=False, enable_webrender=False):
        self.runner_class = Runner
        self.app_args = app_args or []
        self.runner = None
        self.symbols_path = symbols_path
        self.binary = bin

        self.marionette_host = host
        self.marionette_port = port
        self.addons = addons
        self.prefs = prefs
        self.required_prefs = deepcopy(self.required_prefs)
        if prefs:
            self.required_prefs.update(prefs)

        self._gecko_log_option = gecko_log
        self._gecko_log = None
        self.verbose = verbose
        self.headless = headless
        self.enable_webrender = enable_webrender

        # keep track of errors to decide whether instance is unresponsive
        self.unresponsive_count = 0

        # Alternative to default temporary directory
        self.workspace = workspace

        # Don't use the 'profile' property here, because sub-classes could add
        # further preferences and data, which would not be included in the new
        # profile
        self._profile = profile

    @property
    def gecko_log(self):
        if self._gecko_log:
            return self._gecko_log

        path = self._gecko_log_option
        if path != "-":
            if path is None:
                path = "gecko.log"
            elif os.path.isdir(path):
                fname = "gecko-{}.log".format(time.time())
                path = os.path.join(path, fname)

            path = os.path.realpath(path)
            if os.access(path, os.F_OK):
                os.remove(path)

        self._gecko_log = path
        return self._gecko_log

    @property
    def profile(self):
        return self._profile

    @profile.setter
    def profile(self, value):
        self._update_profile(value)

    def _update_profile(self, profile=None, profile_name=None):
        """Check if the profile has to be created, or replaced

        :param profile: A Profile instance to be used.
        :param name: Profile name to be used in the path.
        """
        if self.runner and self.runner.is_running():
            raise errors.MarionetteException("The current profile can only be updated "
                                             "when the instance is not running")

        if isinstance(profile, Profile):
            # Only replace the profile if it is not the current one
            if hasattr(self, "_profile") and profile is self._profile:
                return

        else:
            profile_args = self.profile_args
            profile_path = profile

            # If a path to a profile is given then clone it
            if isinstance(profile_path, basestring):
                profile_args["path_from"] = profile_path
                profile_args["path_to"] = tempfile.mkdtemp(
                    suffix=u".{}".format(profile_name or os.path.basename(profile_path)),
                    dir=self.workspace)
                # The target must not exist yet
                os.rmdir(profile_args["path_to"])

                profile = Profile.clone(**profile_args)

            # Otherwise create a new profile
            else:
                profile_args["profile"] = tempfile.mkdtemp(
                    suffix=u".{}".format(profile_name or "mozrunner"),
                    dir=self.workspace)
                profile = Profile(**profile_args)
                profile.create_new = True

        if isinstance(self.profile, Profile):
            self.profile.cleanup()

        self._profile = profile

    def switch_profile(self, profile_name=None, clone_from=None):
        """Switch the profile by using the given name, and optionally clone it.

        Compared to :attr:`profile` this method allows to switch the profile
        by giving control over the profile name as used for the new profile. It
        also always creates a new blank profile, or as clone of an existent one.

        :param profile_name: Optional, name of the profile, which will be used
            as part of the profile path (folder name containing the profile).
        :clone_from: Optional, if specified the new profile will be cloned
            based on the given profile. This argument can be an instance of
            ``mozprofile.Profile``, or the path of the profile.
        """
        if isinstance(clone_from, Profile):
            clone_from = clone_from.profile

        self._update_profile(clone_from, profile_name=profile_name)

    @property
    def profile_args(self):
        args = {"preferences": deepcopy(self.required_prefs)}
        args["preferences"]["marionette.port"] = self.marionette_port
        args["preferences"]["marionette.defaultPrefs.port"] = self.marionette_port

        if self.prefs:
            args["preferences"].update(self.prefs)

        if self.verbose:
            level = "Trace" if self.verbose >= 2 else "Debug"
            args["preferences"]["marionette.log.level"] = level
            args["preferences"]["marionette.logging"] = level

        if "-jsdebugger" in self.app_args:
            args["preferences"].update({
                "devtools.browsertoolbox.panel": "jsdebugger",
                "devtools.debugger.remote-enabled": True,
                "devtools.chrome.enabled": True,
                "devtools.debugger.prompt-connection": False,
                "marionette.debugging.clicktostart": True,
            })

        if self.addons:
            args["addons"] = self.addons

        return args

    @classmethod
    def create(cls, app=None, *args, **kwargs):
        try:
            if not app and kwargs["bin"] is not None:
                app_id = mozversion.get_version(binary=kwargs["bin"])["application_id"]
                app = app_ids[app_id]

            instance_class = apps[app]
        except (IOError, KeyError):
            exc, val, tb = sys.exc_info()
            msg = 'Application "{0}" unknown (should be one of {1})'
            reraise(NotImplementedError, msg.format(app, apps.keys()), tb)

        return instance_class(*args, **kwargs)

    def start(self):
        self._update_profile(self.profile)
        self.runner = self.runner_class(**self._get_runner_args())
        self.runner.start()

    def _get_runner_args(self):
        process_args = {
            "processOutputLine": [NullOutput()],
        }

        if self.gecko_log == "-":
            process_args["stream"] = sys.stdout
        else:
            process_args["logfile"] = self.gecko_log

        env = os.environ.copy()

        if self.headless:
            env["MOZ_HEADLESS"] = "1"
            env["DISPLAY"] = "77"  # Set a fake display.

        if self.enable_webrender:
            env["MOZ_WEBRENDER"] = "1"
            env["MOZ_ACCELERATED"] = "1"
        else:
            env["MOZ_WEBRENDER"] = "0"

        # environment variables needed for crashreporting
        # https://developer.mozilla.org/docs/Environment_variables_affecting_crash_reporting
        env.update({"MOZ_CRASHREPORTER": "1",
                    "MOZ_CRASHREPORTER_NO_REPORT": "1",
                    "MOZ_CRASHREPORTER_SHUTDOWN": "1",
                    })

        return {
            "binary": self.binary,
            "profile": self.profile,
            "cmdargs": ["-no-remote", "-marionette"] + self.app_args,
            "env": env,
            "symbols_path": self.symbols_path,
            "process_args": process_args
        }

    def close(self, clean=False):
        """
        Close the managed Gecko process.

        Depending on self.runner_class, setting `clean` to True may also kill
        the emulator process in which this instance is running.

        :param clean: If True, also perform runner cleanup.
        """
        if self.runner:
            self.runner.stop()
            if clean:
                self.runner.cleanup()

        if clean:
            if isinstance(self.profile, Profile):
                self.profile.cleanup()
            self.profile = None

    def restart(self, prefs=None, clean=True):
        """
        Close then start the managed Gecko process.

        :param prefs: Dictionary of preference names and values.
        :param clean: If True, reset the profile before starting.
        """
        if prefs:
            self.prefs = prefs
        else:
            self.prefs = None

        self.close(clean=clean)
        self.start()


class FennecInstance(GeckoInstance):
    fennec_prefs = {
        # Enable output for dump() and chrome console API
        "browser.dom.window.dump.enabled": True,
        "devtools.console.stdout.chrome": True,

        # Disable safebrowsing components
        "browser.safebrowsing.blockedURIs.enabled": False,
        "browser.safebrowsing.downloads.enabled": False,
        "browser.safebrowsing.passwords.enabled": False,
        "browser.safebrowsing.malware.enabled": False,
        "browser.safebrowsing.phishing.enabled": False,

        # Do not restore the last open set of tabs if the browser has crashed
        "browser.sessionstore.resume_from_crash": False,

        # Disable e10s by default
        "browser.tabs.remote.autostart": False,

        # Do not allow background tabs to be zombified, otherwise for tests that
        # open additional tabs, the test harness tab itself might get unloaded
        "browser.tabs.disableBackgroundZombification": True,
    }

    def __init__(self, emulator_binary=None, avd_home=None, avd=None,
                 adb_path=None, serial=None, connect_to_running_emulator=False,
                 package_name=None, env=None, *args, **kwargs):
        required_prefs = deepcopy(FennecInstance.fennec_prefs)
        required_prefs.update(kwargs.get("prefs", {}))

        super(FennecInstance, self).__init__(*args, **kwargs)
        self.required_prefs.update(required_prefs)

        self.runner_class = FennecEmulatorRunner
        # runner args
        self._package_name = package_name
        self.emulator_binary = emulator_binary
        self.avd_home = avd_home
        self.adb_path = adb_path
        self.avd = avd
        self.env = env
        self.serial = serial
        self.connect_to_running_emulator = connect_to_running_emulator

    @property
    def package_name(self):
        """
        Name of app to run on emulator.

        Note that FennecInstance does not use self.binary
        """
        if self._package_name is None:
            self._package_name = "org.mozilla.fennec"
            user = os.getenv("USER")
            if user:
                self._package_name += "_" + user
        return self._package_name

    def start(self):
        self._update_profile(self.profile)
        self.runner = self.runner_class(**self._get_runner_args())
        try:
            if self.connect_to_running_emulator:
                self.runner.device.connect()
            self.runner.start()
        except Exception as e:
            exc, val, tb = sys.exc_info()
            message = "Error possibly due to runner or device args: {}"
            reraise(exc, message.format(e.message), tb)

        # forward marionette port
        self.runner.device.device.forward(
            local="tcp:{}".format(self.marionette_port),
            remote="tcp:{}".format(self.marionette_port))

    def _get_runner_args(self):
        process_args = {
            "processOutputLine": [NullOutput()],
        }

        env = {} if self.env is None else self.env.copy()
        if self.enable_webrender:
            env["MOZ_WEBRENDER"] = "1"
        else:
            env["MOZ_WEBRENDER"] = "0"

        runner_args = {
            "app": self.package_name,
            "avd_home": self.avd_home,
            "adb_path": self.adb_path,
            "binary": self.emulator_binary,
            "env": env,
            "profile": self.profile,
            "cmdargs": ["-marionette"] + self.app_args,
            "symbols_path": self.symbols_path,
            "process_args": process_args,
            "logdir": self.workspace or os.getcwd(),
            "serial": self.serial,
        }
        if self.avd:
            runner_args["avd"] = self.avd

        return runner_args

    def close(self, clean=False):
        """
        Close the managed Gecko process.

        If `clean` is True and the Fennec instance is running in an
        emulator managed by mozrunner, this will stop the emulator.

        :param clean: If True, also perform runner cleanup.
        """
        super(FennecInstance, self).close(clean)
        if clean and self.runner and self.runner.device.connected:
            try:
                self.runner.device.device.remove_forwards(
                    "tcp:{}".format(self.marionette_port))
                self.unresponsive_count = 0
            except Exception:
                self.unresponsive_count += 1
                traceback.print_exception(*sys.exc_info())


class DesktopInstance(GeckoInstance):
    desktop_prefs = {
        # Disable Firefox old build background check
        "app.update.checkInstallTime": False,

        # Disable automatically upgrading Firefox
        #
        # Note: Possible update tests could reset or flip the value to allow
        # updates to be downloaded and applied.
        "app.update.disabledForTesting": True,
        # !!! For backward compatibility up to Firefox 64. Only remove
        # when this Firefox version is no longer supported by the client !!!
        "app.update.auto": 0,

        # Don't show the content blocking introduction panel
        # We use a larger number than the default 22 to have some buffer
        "browser.contentblocking.introCount": 99,

        # Enable output for dump() and chrome console API
        "browser.dom.window.dump.enabled": True,
        "devtools.console.stdout.chrome": True,

        # Indicate that the download panel has been shown once so that whichever
        # download test runs first doesn"t show the popup inconsistently
        "browser.download.panel.shown": True,

        # Do not show the EULA notification which can interfer with tests
        "browser.EULA.override": True,

        # Always display a blank page
        "browser.newtabpage.enabled": False,

        # Background thumbnails in particular cause grief, and disabling thumbnails
        # in general can"t hurt - we re-enable them when tests need them
        "browser.pagethumbnails.capturing_disabled": True,

        # Disable safebrowsing components
        "browser.safebrowsing.blockedURIs.enabled": False,
        "browser.safebrowsing.downloads.enabled": False,
        "browser.safebrowsing.passwords.enabled": False,
        "browser.safebrowsing.malware.enabled": False,
        "browser.safebrowsing.phishing.enabled": False,

        # Do not restore the last open set of tabs if the browser has crashed
        "browser.sessionstore.resume_from_crash": False,

        # Don't check for the default web browser during startup
        "browser.shell.checkDefaultBrowser": False,

        # Disable e10s by default
        "browser.tabs.remote.autostart": False,

        # Needed for branded builds to prevent opening a second tab on startup
        "browser.startup.homepage_override.mstone": "ignore",
        # Start with a blank page by default
        "browser.startup.page": 0,

        # Disable browser animations
        "toolkit.cosmeticAnimations.enabled": False,

        # Bug 1557457: Disable because modal dialogs might not appear in Firefox
        "browser.tabs.remote.separatePrivilegedContentProcess": False,

        # Don't unload tabs when available memory is running low
        "browser.tabs.unloadOnLowMemory": False,

        # Do not warn when closing all open tabs
        "browser.tabs.warnOnClose": False,
        # Do not warn when closing all other open tabs
        "browser.tabs.warnOnCloseOtherTabs": False,
        # Do not warn when multiple tabs will be opened
        "browser.tabs.warnOnOpen": False,

        # Disable the UI tour
        "browser.uitour.enabled": False,

        # Turn off search suggestions in the location bar so as not to trigger network
        # connections.
        "browser.urlbar.suggest.searches": False,

        # Turn off the location bar search suggestions opt-in.  It interferes with
        # tests that don't expect it to be there.
        "browser.urlbar.userMadeSearchSuggestionsChoice": True,

        # Don't warn when exiting the browser
        "browser.warnOnQuit": False,

        # Disable first-run welcome page
        "startup.homepage_welcome_url": "about:blank",
        "startup.homepage_welcome_url.additional": "",
    }

    def __init__(self, *args, **kwargs):
        required_prefs = deepcopy(DesktopInstance.desktop_prefs)
        required_prefs.update(kwargs.get("prefs", {}))

        super(DesktopInstance, self).__init__(*args, **kwargs)
        self.required_prefs.update(required_prefs)


class NullOutput(object):
    def __call__(self, line):
        pass


apps = {
    'fennec': FennecInstance,
    'fxdesktop': DesktopInstance,
}

app_ids = {
    '{aa3c5121-dab2-40e2-81ca-7ea25febc110}': 'fennec',
    '{ec8030f7-c20a-464f-9b0e-13a3a9e97384}': 'fxdesktop',
}
