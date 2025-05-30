# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifdef XP_UNIX
#ifndef XP_MACOSX
#define XP_GNOME 1
#endif
#endif

  <stringbundleset id="stringbundleset"
                   string-bookmarkthispage="&bookmarkThisPageCmd.label;"
                   string-editthisbookmark="&editThisBookmarkCmd.label;">
    <stringbundle id="bundle_brand" src="chrome://branding/locale/brand.properties"/>
    <stringbundle id="bundle_shell" src="chrome://browser/locale/shellservice.properties"/>
  </stringbundleset>

  <commandset id="mainCommandSet">
    <command id="cmd_newNavigator" oncommand="OpenBrowserWindow()"/>
    <command id="cmd_handleBackspace" oncommand="BrowserHandleBackspace();" />
    <command id="cmd_handleShiftBackspace" oncommand="BrowserHandleShiftBackspace();" />

    <command id="cmd_newNavigatorTab" oncommand="BrowserOpenTab(event);"/>
    <command id="cmd_newNavigatorTabNoEvent" oncommand="BrowserOpenTab();"/>
    <command id="Browser:OpenFile"  oncommand="BrowserOpenFileWindow();"/>
    <command id="Browser:SavePage" oncommand="saveBrowser(gBrowser.selectedBrowser);"/>

    <command id="Browser:SendLink"
             oncommand="MailIntegration.sendLinkForBrowser(gBrowser.selectedBrowser);"/>

    <command id="cmd_pageSetup" oncommand="PrintUtils.showPageSetup();"/>
    <command id="cmd_print" oncommand="PrintUtils.printWindow(window.gBrowser.selectedBrowser.outerWindowID, window.gBrowser.selectedBrowser);"/>
    <command id="cmd_printPreview" oncommand="PrintUtils.printPreview(PrintPreviewListener);"/>
    <command id="cmd_importFromAnotherBrowser" oncommand="MigrationUtils.showMigrationWizard(window, [MigrationUtils.MIGRATION_ENTRYPOINT_FILE_MENU]);"/>
    <command id="cmd_close" oncommand="BrowserCloseTabOrWindow(event);"/>
    <command id="cmd_closeWindow" oncommand="BrowserTryToCloseWindow()"/>
    <command id="cmd_toggleMute" oncommand="gBrowser.toggleMuteAudioOnMultiSelectedTabs(gBrowser.selectedTab)"/>
    <command id="cmd_CustomizeToolbars" oncommand="gCustomizeMode.enter()"/>
    <command id="cmd_toggleOfflineStatus" oncommand="BrowserOffline.toggleOfflineStatus();"/>
    <command id="cmd_quitApplication" oncommand="goQuitApplication()"/>

    <command id="View:PageSource" oncommand="BrowserViewSource(window.gBrowser.selectedBrowser);"/>
    <command id="View:PageInfo" oncommand="BrowserPageInfo();"/>
    <command id="View:FullScreen" oncommand="BrowserFullScreen();"/>
    <command id="View:ReaderView" oncommand="ReaderParent.toggleReaderMode(event);"/>
    <command id="View:PictureInPicture" oncommand="PictureInPicture.onCommand(event);"/>
    <command id="cmd_find" oncommand="gLazyFindCommand('onFindCommand')"/>
    <command id="cmd_findAgain" oncommand="gLazyFindCommand('onFindAgainCommand', false)"/>
    <command id="cmd_findPrevious" oncommand="gLazyFindCommand('onFindAgainCommand', true)"/>
#ifdef XP_MACOSX
    <command id="cmd_findSelection" oncommand="gLazyFindCommand('onFindSelectionCommand')"/>
#endif
    <!-- work-around bug 392512 -->
    <command id="Browser:AddBookmarkAs"
             oncommand="PlacesCommandHook.bookmarkPage();"/>
    <command id="Browser:BookmarkAllTabs"
             oncommand="PlacesCommandHook.bookmarkPages(PlacesCommandHook.uniqueCurrentPages);"/>
    <command id="Browser:Back"    oncommand="BrowserBack();" disabled="true"/>
    <command id="Browser:BackOrBackDuplicate" oncommand="BrowserBack(event);" disabled="true">
      <observes element="Browser:Back" attribute="disabled"/>
    </command>
    <command id="Browser:Forward" oncommand="BrowserForward();" disabled="true"/>
    <command id="Browser:ForwardOrForwardDuplicate" oncommand="BrowserForward(event);" disabled="true">
      <observes element="Browser:Forward" attribute="disabled"/>
    </command>
    <command id="Browser:Stop"    oncommand="BrowserStop();" disabled="true"/>
    <command id="Browser:Reload"  oncommand="if (event.shiftKey) BrowserReloadSkipCache(); else BrowserReload()" disabled="true"/>
    <command id="Browser:ReloadOrDuplicate" oncommand="BrowserReloadOrDuplicate(event)" disabled="true">
      <observes element="Browser:Reload" attribute="disabled"/>
    </command>
    <command id="Browser:ReloadSkipCache" oncommand="BrowserReloadSkipCache()" disabled="true">
      <observes element="Browser:Reload" attribute="disabled"/>
    </command>
    <command id="Browser:NextTab" oncommand="gBrowser.tabContainer.advanceSelectedTab(1, true);"/>
    <command id="Browser:PrevTab" oncommand="gBrowser.tabContainer.advanceSelectedTab(-1, true);"/>
    <command id="Browser:ShowAllTabs" oncommand="gTabsPanel.showAllTabsPanel();"/>
    <command id="cmd_fullZoomReduce"  oncommand="FullZoom.reduce()"/>
    <command id="cmd_fullZoomEnlarge" oncommand="FullZoom.enlarge()"/>
    <command id="cmd_fullZoomReset"   oncommand="FullZoom.reset()"/>
    <command id="cmd_fullZoomToggle"  oncommand="ZoomManager.toggleZoom();"/>
    <command id="cmd_gestureRotateLeft" oncommand="gGestureSupport.rotate(event.sourceEvent)"/>
    <command id="cmd_gestureRotateRight" oncommand="gGestureSupport.rotate(event.sourceEvent)"/>
    <command id="cmd_gestureRotateEnd" oncommand="gGestureSupport.rotateEnd()"/>
    <command id="Browser:OpenLocation" oncommand="openLocation();"/>
    <command id="Browser:RestoreLastSession" oncommand="SessionStore.restoreLastSession();" disabled="true"/>
    <command id="Browser:NewUserContextTab" oncommand="openNewUserContextTab(event.sourceEvent);"/>
    <command id="Browser:OpenAboutContainers" oncommand="openPreferences('paneContainers');"/>
    <command id="Tools:Search" oncommand="BrowserSearch.webSearch();"/>
    <command id="Tools:Downloads" oncommand="BrowserDownloadsUI();"/>
    <command id="Tools:Addons" oncommand="BrowserOpenAddonsMgr();"/>
    <command id="Tools:Sanitize" oncommand="Sanitizer.showUI(window);"/>
    <command id="Tools:PrivateBrowsing"
      oncommand="OpenBrowserWindow({private: true});"/>
    <command id="History:UndoCloseTab" oncommand="undoCloseTab();"/>
    <command id="History:UndoCloseWindow" oncommand="undoCloseWindow();"/>

#ifdef NIGHTLY_BUILD
    <command id="wrCaptureCmd" oncommand="gGfxUtils.webrenderCapture();"/>
    <command id="windowRecordingCmd" oncommand="gGfxUtils.toggleWindowRecording();"/>
#endif
#ifdef XP_MACOSX
    <command id="minimizeWindow"
             label="&minimizeWindow.label;"
             oncommand="window.minimize();" />
    <command id="zoomWindow"
             label="&zoomWindow.label;"
             oncommand="zoomWindow();" />
#endif
  </commandset>

#include ../../components/places/content/placesCommands.inc.xhtml

  <keyset id="mainKeyset">
    <key id="key_newNavigator"
         key="&newNavigatorCmd.key;"
         command="cmd_newNavigator"
         modifiers="accel" reserved="true"/>
    <key id="key_newNavigatorTab" key="&tabCmd.commandkey;" modifiers="accel"
         command="cmd_newNavigatorTabNoEvent" reserved="true"/>
    <key id="focusURLBar" key="&openCmd.commandkey;" command="Browser:OpenLocation"
         modifiers="accel"/>
#ifndef XP_MACOSX
    <key id="focusURLBar2" key="&urlbar.accesskey;" command="Browser:OpenLocation"
         modifiers="alt"/>
#endif

#
# Search Command Key Logic works like this:
#
# Unix: Ctrl+K (cross platform binding)
#       Ctrl+J (in case of emacs Ctrl-K conflict)
# Mac:  Cmd+K (cross platform binding)
#       Cmd+Opt+F (platform convention)
# Win:  Ctrl+K (cross platform binding)
#       Ctrl+E (IE compat)
#
# We support Ctrl+K on all platforms now and advertise it in the menu since it is
# our standard - it is a "safe" choice since it is near no harmful keys like "W" as
# "E" is. People mourning the loss of Ctrl+K for emacs compat can switch their GTK
# system setting to use emacs emulation, and we should respect it. Focus-Search-Box
# is a fundamental keybinding and we are maintaining a XP binding so that it is easy
# for people to switch to Linux.
#
    <key id="key_search" key="&searchFocus.commandkey;" command="Tools:Search" modifiers="accel"/>
#ifdef XP_MACOSX
    <key id="key_search2" key="&findOnCmd.commandkey;" command="Tools:Search" modifiers="accel,alt"/>
#endif
#ifdef XP_WIN
    <key id="key_search2" key="&searchFocus.commandkey2;" command="Tools:Search" modifiers="accel"/>
#endif
#ifdef XP_GNOME
    <key id="key_search2" key="&searchFocusUnix.commandkey;" command="Tools:Search" modifiers="accel"/>
    <key id="key_openDownloads" key="&downloadsUnix.commandkey;" command="Tools:Downloads" modifiers="accel,shift"/>
#else
    <key id="key_openDownloads" key="&downloads.commandkey;" command="Tools:Downloads" modifiers="accel"/>
#endif
    <key id="key_openAddons" key="&addons.commandkey;" command="Tools:Addons" modifiers="accel,shift"/>
    <key id="openFileKb" key="&openFileCmd.commandkey;" command="Browser:OpenFile"  modifiers="accel"/>
    <key id="key_savePage" key="&savePageCmd.commandkey;" command="Browser:SavePage" modifiers="accel"/>
    <key id="printKb" key="&printCmd.commandkey;" command="cmd_print"  modifiers="accel"/>
    <key id="key_close" key="&closeCmd.key;" command="cmd_close" modifiers="accel" reserved="true"/>
    <key id="key_closeWindow" key="&closeCmd.key;" command="cmd_closeWindow" modifiers="accel,shift" reserved="true"/>
    <key id="key_toggleMute" key="&toggleMuteCmd.key;" command="cmd_toggleMute" modifiers="control"/>
    <key id="key_undo"
         key="&undoCmd.key;"
         modifiers="accel"/>
#ifdef XP_UNIX
    <key id="key_redo" key="&undoCmd.key;" modifiers="accel,shift"/>
#else
    <key id="key_redo" key="&redoCmd.key;" modifiers="accel"/>
#endif
    <key id="key_cut"
         key="&cutCmd.key;"
         modifiers="accel"/>
    <key id="key_copy"
         key="&copyCmd.key;"
         modifiers="accel"/>
    <key id="key_paste"
         key="&pasteCmd.key;"
         modifiers="accel"/>
    <key id="key_delete" keycode="VK_DELETE" command="cmd_delete"/>
    <key id="key_selectAll" key="&selectAllCmd.key;" modifiers="accel"/>

    <key keycode="VK_BACK" command="cmd_handleBackspace" reserved="false"/>
    <key keycode="VK_BACK" command="cmd_handleShiftBackspace" modifiers="shift" reserved="false"/>
#ifndef XP_MACOSX
    <key id="goBackKb"  keycode="VK_LEFT" command="Browser:Back" modifiers="alt"/>
    <key id="goForwardKb"  keycode="VK_RIGHT" command="Browser:Forward" modifiers="alt"/>
#else
    <key id="goBackKb" keycode="VK_LEFT" command="Browser:Back" modifiers="accel" />
    <key id="goForwardKb" keycode="VK_RIGHT" command="Browser:Forward" modifiers="accel" />
#endif
#ifdef XP_UNIX
    <key id="goBackKb2" key="&goBackCmd.commandKey;" command="Browser:Back" modifiers="accel"/>
    <key id="goForwardKb2" key="&goForwardCmd.commandKey;" command="Browser:Forward" modifiers="accel"/>
#endif
    <key id="goHome" keycode="VK_HOME" oncommand="BrowserHome();" modifiers="alt"/>
    <key keycode="VK_F5" command="Browser:Reload"/>
#ifndef XP_MACOSX
    <key id="showAllHistoryKb" key="&showAllHistoryCmd.commandkey;" command="Browser:ShowAllHistory" modifiers="accel,shift"/>
    <key keycode="VK_F5" command="Browser:ReloadSkipCache" modifiers="accel"/>
    <key id="key_fullScreen" keycode="VK_F11" command="View:FullScreen"/>
#else
    <key id="key_fullScreen" key="&fullScreenCmd.macCommandKey;" command="View:FullScreen" modifiers="accel,control"/>
    <key id="key_fullScreen_old" key="&fullScreenCmd.macCommandKey;" command="View:FullScreen" modifiers="accel,shift"/>
    <key keycode="VK_F11" command="View:FullScreen"/>
#endif
#ifndef XP_WIN
    <key id="key_toggleReaderMode" key="&toggleReaderMode.key;" command="View:ReaderView" modifiers="accel,alt" disabled="true"/>
#else
    <key id="key_toggleReaderMode" keycode="&toggleReaderMode.win.keycode;" command="View:ReaderView" disabled="true"/>
#endif

#ifdef XP_WIN
    <key id="key_togglePictureInPicture" key="&togglePictureInPicture.key;" command="View:PictureInPicture" modifiers="accel,shift"/>
    <key                                 key="&togglePictureInPicture.key2;" command="View:PictureInPicture" modifiers="accel,shift"/>
#endif

    <key key="&reloadCmd.commandkey;" command="Browser:Reload" modifiers="accel" id="key_reload"/>
    <key key="&reloadCmd.commandkey;" command="Browser:ReloadSkipCache" modifiers="accel,shift"/>
    <key id="key_viewSource" key="&pageSourceCmd.commandkey;" command="View:PageSource" modifiers="accel"/>
#ifdef XP_MACOSX
    <key id="key_viewSourceSafari" key="&pageSourceCmd.SafariCommandKey;" command="View:PageSource" modifiers="accel,alt"/>
#endif
#ifndef XP_WIN
    <key id="key_viewInfo"   key="&pageInfoCmd.commandkey;"   command="View:PageInfo"   modifiers="accel"/>
#endif
    <key id="key_find" key="&findOnCmd.commandkey;" command="cmd_find" modifiers="accel"/>
    <key id="key_findAgain" key="&findAgainCmd.commandkey;" command="cmd_findAgain" modifiers="accel"/>
    <key id="key_findPrevious" key="&findAgainCmd.commandkey;" command="cmd_findPrevious" modifiers="accel,shift"/>
#ifdef XP_MACOSX
    <key id="key_findSelection" key="&findSelectionCmd.commandkey;" command="cmd_findSelection" modifiers="accel"/>
#endif
    <key keycode="&findAgainCmd.commandkey2;" command="cmd_findAgain"/>
    <key keycode="&findAgainCmd.commandkey2;"  command="cmd_findPrevious" modifiers="shift"/>

    <key id="addBookmarkAsKb" key="&bookmarkThisPageCmd.commandkey;" command="Browser:AddBookmarkAs" modifiers="accel"/>
    <key id="bookmarkAllTabsKb" key="&bookmarkThisPageCmd.commandkey;" oncommand="PlacesCommandHook.bookmarkPages(PlacesCommandHook.uniqueCurrentPages);" modifiers="accel,shift"/>
# Accel+Shift+A-F are reserved on GTK
#ifndef MOZ_WIDGET_GTK
    <key id="manBookmarkKb" key="&bookmarksCmd.commandkey;" command="Browser:ShowAllBookmarks" modifiers="accel,shift"/>
#else
    <key id="manBookmarkKb" key="&bookmarksGtkCmd.commandkey;" command="Browser:ShowAllBookmarks" modifiers="accel,shift"/>
#endif
    <key id="viewBookmarksSidebarKb" key="&bookmarksCmd.commandkey;" oncommand="SidebarUI.toggle('viewBookmarksSidebar');" modifiers="accel"/>
#ifdef XP_WIN
# Cmd+I is conventially mapped to Info on MacOS X, thus it should not be
# overridden for other purposes there.
    <key id="viewBookmarksSidebarWinKb" key="&bookmarksWinCmd.commandkey;" oncommand="SidebarUI.toggle('viewBookmarksSidebar');" modifiers="accel"/>
#endif

    <key id="key_stop" keycode="VK_ESCAPE" command="Browser:Stop"/>

#ifdef XP_MACOSX
    <key id="key_stop_mac" modifiers="accel" key="&stopCmd.macCommandKey;" command="Browser:Stop"/>
#endif

    <key id="key_gotoHistory"
         key="&historySidebarCmd.commandKey;"
#ifdef XP_MACOSX
         modifiers="accel,shift"
#else
         modifiers="accel"
#endif
         oncommand="SidebarUI.toggle('viewHistorySidebar');"/>

    <key id="key_fullZoomReduce"  key="&fullZoomReduceCmd.commandkey;"   command="cmd_fullZoomReduce"  modifiers="accel"/>
    <key                          key="&fullZoomReduceCmd.commandkey2;"  command="cmd_fullZoomReduce"  modifiers="accel"/>
    <key id="key_fullZoomEnlarge" key="&fullZoomEnlargeCmd.commandkey;"  command="cmd_fullZoomEnlarge" modifiers="accel"/>
    <key                          key="&fullZoomEnlargeCmd.commandkey2;" command="cmd_fullZoomEnlarge" modifiers="accel"/>
    <key                          key="&fullZoomEnlargeCmd.commandkey3;" command="cmd_fullZoomEnlarge" modifiers="accel"/>
    <key id="key_fullZoomReset"   key="&fullZoomResetCmd.commandkey;"    command="cmd_fullZoomReset"   modifiers="accel"/>
    <key                          key="&fullZoomResetCmd.commandkey2;"   command="cmd_fullZoomReset"   modifiers="accel"/>

    <key id="key_showAllTabs" keycode="VK_TAB" modifiers="control,shift"/>

    <key id="key_switchTextDirection" key="&bidiSwitchTextDirectionItem.commandkey;" command="cmd_switchTextDirection" modifiers="accel,shift" />

    <key id="key_privatebrowsing" command="Tools:PrivateBrowsing" key="&privateBrowsingCmd.commandkey;"
         modifiers="accel,shift" reserved="true"/>
    <key id="key_sanitize" command="Tools:Sanitize" keycode="VK_DELETE" modifiers="accel,shift"/>
#ifdef XP_MACOSX
    <key id="key_sanitize_mac" command="Tools:Sanitize" keycode="VK_BACK" modifiers="accel,shift"/>
#endif
    <key id="key_quitApplication" key="&quitApplicationCmd.key;"
#ifdef XP_WIN
         modifiers="accel,shift"
#else
         modifiers="accel"
#endif
# On OS X, dark voodoo magic invokes the quit code for this key.
# So we're not adding the attribute on OSX because of backwards/add-on compat.
# See bug 1369909 for background on this.
#ifndef XP_MACOSX
         command="cmd_quitApplication"
#endif
         reserved="true"/>

    <key id="key_undoCloseTab" command="History:UndoCloseTab" key="&tabCmd.commandkey;" modifiers="accel,shift"/>
    <key id="key_undoCloseWindow" command="History:UndoCloseWindow" key="&newNavigatorCmd.key;" modifiers="accel,shift"/>

#ifdef XP_GNOME
#define NUM_SELECT_TAB_MODIFIER alt
#else
#define NUM_SELECT_TAB_MODIFIER accel
#endif

#expand    <key id="key_selectTab1" oncommand="gBrowser.selectTabAtIndex(0, event);" key="1" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab2" oncommand="gBrowser.selectTabAtIndex(1, event);" key="2" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab3" oncommand="gBrowser.selectTabAtIndex(2, event);" key="3" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab4" oncommand="gBrowser.selectTabAtIndex(3, event);" key="4" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab5" oncommand="gBrowser.selectTabAtIndex(4, event);" key="5" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab6" oncommand="gBrowser.selectTabAtIndex(5, event);" key="6" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab7" oncommand="gBrowser.selectTabAtIndex(6, event);" key="7" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectTab8" oncommand="gBrowser.selectTabAtIndex(7, event);" key="8" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>
#expand    <key id="key_selectLastTab" oncommand="gBrowser.selectTabAtIndex(-1, event);" key="9" modifiers="__NUM_SELECT_TAB_MODIFIER__"/>

#ifdef NIGHTLY_BUILD
    <key id="key_wrCaptureCmd"
#ifdef XP_MACOSX
    key="3" modifiers="control,shift"
#else
    key="#" modifiers="control"
#endif
    command="wrCaptureCmd"/>
    <key id="key_windowRecordingCmd"
#ifdef XP_MACOSX
    key="4" modifiers="control,shift"
#else
    key="$" modifiers="control"
#endif
    command="windowRecordingCmd"/>
#endif
#ifdef XP_MACOSX
    <key id="key_minimizeWindow"
         command="minimizeWindow"
         key="&minimizeWindow.key;"
         modifiers="accel"/>
    <key id="key_openHelpMac"
         oncommand="openHelpLink('');"
         key="&helpMac.commandkey;"
         modifiers="accel"/>
    <!-- These are used to build the Application menu -->
    <key id="key_preferencesCmdMac"
         key="&preferencesCmdMac.commandkey;"
         reserved="true"
         modifiers="accel"/>
    <key id="key_hideThisAppCmdMac"
         key="&hideThisAppCmdMac2.commandkey;"
         modifiers="accel"/>
    <key id="key_hideOtherAppsCmdMac"
         key="&hideOtherAppsCmdMac.commandkey;"
         modifiers="accel,alt"/>
#endif
  </keyset>
