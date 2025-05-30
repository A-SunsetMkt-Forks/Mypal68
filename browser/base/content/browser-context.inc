# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

      <menugroup id="context-navigation">
        <menuitem id="context-back"
                  data-l10n-id="main-context-menu-back"
                  class="menuitem-iconic"
                  command="Browser:BackOrBackDuplicate"
                  onclick="checkForMiddleClick(this, event);"/>
        <menuitem id="context-forward"
                  data-l10n-id="main-context-menu-forward"
                  class="menuitem-iconic"
                  command="Browser:ForwardOrForwardDuplicate"
                  onclick="checkForMiddleClick(this, event);"/>
        <menuitem id="context-reload"
                  class="menuitem-iconic"
                  tooltip="dynamic-shortcut-tooltip"
                  data-l10n-id="main-context-menu-reload"
                  command="Browser:ReloadOrDuplicate"
                  onclick="checkForMiddleClick(this, event);"/>
        <menuitem id="context-stop"
                  class="menuitem-iconic"
                  tooltip="dynamic-shortcut-tooltip"
                  data-l10n-id="main-context-menu-stop"
                  command="Browser:Stop"/>
        <menuitem id="context-bookmarkpage"
                  class="menuitem-iconic"
                  data-l10n-id="main-context-menu-bookmark-page"
                  oncommand="gContextMenu.bookmarkThisPage();"/>
      </menugroup>
      <menuseparator id="context-sep-navigation"/>
      <menuseparator id="page-menu-separator"/>
      <menuitem id="spell-no-suggestions"
                disabled="true"
                label="&spellNoSuggestions.label;"/>
      <menuitem id="spell-add-to-dictionary"
                label="&spellAddToDictionary.label;"
                accesskey="&spellAddToDictionary.accesskey;"
                oncommand="InlineSpellCheckerUI.addToDictionary();"/>
      <menuitem id="spell-undo-add-to-dictionary"
                label="&spellUndoAddToDictionary.label;"
                accesskey="&spellUndoAddToDictionary.accesskey;"
                oncommand="InlineSpellCheckerUI.undoAddToDictionary();" />
      <menuseparator id="spell-suggestions-separator"/>
      <menuitem id="context-openlinkincurrent"
                data-l10n-id="main-context-menu-open-link"
                oncommand="gContextMenu.openLinkInCurrent();"/>
# label and data-usercontextid are dynamically set.
      <menuitem id="context-openlinkincontainertab"
                accesskey="&openLinkCmdInTab.accesskey;"
                oncommand="gContextMenu.openLinkInTab(event);"/>
      <menuitem id="context-openlinkintab"
                data-l10n-id="main-context-menu-open-link-new-tab"
                data-usercontextid="0"
                oncommand="gContextMenu.openLinkInTab(event);"/>

      <menu id="context-openlinkinusercontext-menu"
            data-l10n-id="main-context-menu-open-link-container-tab"
            hidden="true">
        <menupopup oncommand="gContextMenu.openLinkInTab(event);"
                   onpopupshowing="return gContextMenu.createContainerMenu(event);" />
      </menu>

      <menuitem id="context-openlink"
                data-l10n-id="main-context-menu-open-link-new-window"
                oncommand="gContextMenu.openLink();"/>
      <menuitem id="context-openlinkprivate"
                data-l10n-id="main-context-menu-open-link-new-private-window"
                oncommand="gContextMenu.openLinkInPrivateWindow();"/>
      <menuseparator id="context-sep-open"/>
      <menuitem id="context-bookmarklink"
                data-l10n-id="main-context-menu-bookmark-this-link"
                oncommand="gContextMenu.bookmarkLink();"/>
      <menuitem id="context-savelink"
                data-l10n-id="main-context-menu-save-link"
                oncommand="gContextMenu.saveLink();"/>
      <menuitem id="context-copyemail"
                data-l10n-id="main-context-menu-copy-email"
                oncommand="gContextMenu.copyEmail();"/>
      <menuitem id="context-copylink"
                data-l10n-id="main-context-menu-copy-link"
                oncommand="gContextMenu.copyLink();"/>
      <menuseparator id="context-sep-copylink"/>
      <menuitem id="context-media-play"
                data-l10n-id="main-context-menu-media-play"
                oncommand="gContextMenu.mediaCommand('play');"/>
      <menuitem id="context-media-pause"
                data-l10n-id="main-context-menu-media-pause"
                oncommand="gContextMenu.mediaCommand('pause');"/>
      <menuitem id="context-media-mute"
                data-l10n-id="main-context-menu-media-mute"
                oncommand="gContextMenu.mediaCommand('mute');"/>
      <menuitem id="context-media-unmute"
                data-l10n-id="main-context-menu-media-unmute"
                oncommand="gContextMenu.mediaCommand('unmute');"/>
      <menu id="context-media-playbackrate" data-l10n-id="main-context-menu-media-play-speed">
        <menupopup>
          <menuitem id="context-media-playbackrate-050x"
                    data-l10n-id="main-context-menu-media-play-speed-slow"
                    type="radio"
                    name="playbackrate"
                    oncommand="gContextMenu.mediaCommand('playbackRate', 0.5);"/>
          <menuitem id="context-media-playbackrate-100x"
                    data-l10n-id="main-context-menu-media-play-speed-normal"
                    type="radio"
                    name="playbackrate"
                    checked="true"
                    oncommand="gContextMenu.mediaCommand('playbackRate', 1.0);"/>
          <menuitem id="context-media-playbackrate-125x"
                    data-l10n-id="main-context-menu-media-play-speed-fast"
                    type="radio"
                    name="playbackrate"
                    oncommand="gContextMenu.mediaCommand('playbackRate', 1.25);"/>
          <menuitem id="context-media-playbackrate-150x"
                    data-l10n-id="main-context-menu-media-play-speed-faster"
                    type="radio"
                    name="playbackrate"
                    oncommand="gContextMenu.mediaCommand('playbackRate', 1.5);"/>
          <menuitem id="context-media-playbackrate-200x"
                    data-l10n-id="main-context-menu-media-play-speed-fastest"
                    type="radio"
                    name="playbackrate"
                    oncommand="gContextMenu.mediaCommand('playbackRate', 2.0);"/>
        </menupopup>
      </menu>
      <menuitem id="context-media-loop"
                data-l10n-id="main-context-menu-media-loop"
                type="checkbox"
                oncommand="gContextMenu.mediaCommand('loop');"/>
      <menuitem id="context-media-showcontrols"
                data-l10n-id="main-context-menu-media-show-controls"
                oncommand="gContextMenu.mediaCommand('showcontrols');"/>
      <menuitem id="context-media-hidecontrols"
                data-l10n-id="main-context-menu-media-hide-controls"
                oncommand="gContextMenu.mediaCommand('hidecontrols');"/>
      <menuitem id="context-video-fullscreen"
                data-l10n-id="main-context-menu-media-video-fullscreen"
                oncommand="gContextMenu.mediaCommand('fullscreen');"/>
      <menuitem id="context-leave-dom-fullscreen"
                data-l10n-id="main-context-menu-media-video-leave-fullscreen"
                oncommand="gContextMenu.leaveDOMFullScreen();"/>
      <menuitem id="context-video-pictureinpicture"
                data-l10n-id="main-context-menu-media-pip"
                type="checkbox"
                oncommand="gContextMenu.mediaCommand('pictureinpicture');"/>
      <menuseparator id="context-media-sep-commands"/>
      <menuitem id="context-reloadimage"
                data-l10n-id="main-context-menu-image-reload"
                oncommand="gContextMenu.reloadImage();"/>
      <menuitem id="context-viewimage"
                data-l10n-id="main-context-menu-image-view"
                oncommand="gContextMenu.viewMedia(event);"
                onclick="checkForMiddleClick(this, event);"/>
      <menuitem id="context-viewvideo"
                data-l10n-id="main-context-menu-video-view"
                oncommand="gContextMenu.viewMedia(event);"
                onclick="checkForMiddleClick(this, event);"/>
#ifdef CONTEXT_COPY_IMAGE_CONTENTS
      <menuitem id="context-copyimage-contents"
                data-l10n-id="main-context-menu-image-copy"
                oncommand="goDoCommand('cmd_copyImage');"/>
#endif
      <menuitem id="context-copyimage"
                data-l10n-id="main-context-menu-image-copy-location"
                oncommand="gContextMenu.copyMediaLocation();"/>
      <menuitem id="context-copyvideourl"
                data-l10n-id="main-context-menu-video-copy-location"
                oncommand="gContextMenu.copyMediaLocation();"/>
      <menuitem id="context-copyaudiourl"
                data-l10n-id="main-context-menu-audio-copy-location"
                oncommand="gContextMenu.copyMediaLocation();"/>
      <menuseparator id="context-sep-copyimage"/>
      <menuitem id="context-saveimage"
                data-l10n-id="main-context-menu-image-save-as"
                oncommand="gContextMenu.saveMedia();"/>
      <menuitem id="context-sendimage"
                data-l10n-id="main-context-menu-image-email"
                oncommand="gContextMenu.sendMedia();"/>
      <menuitem id="context-setDesktopBackground"
                data-l10n-id="main-context-menu-image-set-as-background"
                oncommand="gContextMenu.setDesktopBackground();"/>
      <menuitem id="context-viewimageinfo"
                data-l10n-id="main-context-menu-image-info"
                oncommand="gContextMenu.viewImageInfo();"/>
      <menuitem id="context-viewimagedesc"
                data-l10n-id="main-context-menu-image-desc"
                oncommand="gContextMenu.viewImageDesc(event);"
                onclick="checkForMiddleClick(this, event);"/>
      <menuitem id="context-savevideo"
                data-l10n-id="main-context-menu-video-save-as"
                oncommand="gContextMenu.saveMedia();"/>
      <menuitem id="context-saveaudio"
                data-l10n-id="main-context-menu-audio-save-as"
                oncommand="gContextMenu.saveMedia();"/>
      <menuitem id="context-video-saveimage"
                data-l10n-id="main-context-menu-video-image-save-as"
                oncommand="gContextMenu.saveVideoFrameAsImage();"/>
      <menuitem id="context-sendvideo"
                data-l10n-id="main-context-menu-video-email"
                oncommand="gContextMenu.sendMedia();"/>
      <menuitem id="context-sendaudio"
                data-l10n-id="main-context-menu-audio-email"
                oncommand="gContextMenu.sendMedia();"/>
      <menuitem id="context-ctp-play"
                data-l10n-id="main-context-menu-plugin-play"
                oncommand="gContextMenu.playPlugin();"/>
      <menuitem id="context-ctp-hide"
                data-l10n-id="main-context-menu-plugin-hide"
                oncommand="gContextMenu.hidePlugin();"/>
      <menuseparator id="context-sep-ctp"/>
      <menuitem id="context-savepage"
                data-l10n-id="main-context-menu-page-save"
                oncommand="gContextMenu.savePageAs();"/>
      <menuseparator id="context-sep-sendpagetodevice" class="sync-ui-item"
                     hidden="true"/>
      <menu id="context-sendpagetodevice"
            class="sync-ui-item"
            data-l10n-id="main-context-menu-send-to-device"
            hidden="true">
        <menupopup id="context-sendpagetodevice-popup"
                   onpopupshowing="(() => { gSync.populateSendTabToDevicesMenu(event.target, gBrowser.currentURI.spec, gBrowser.contentTitle); })()"/>
      </menu>
      <menuseparator id="context-sep-viewbgimage"/>
      <menuitem id="context-viewbgimage"
                data-l10n-id="main-context-menu-view-background-image"
                oncommand="gContextMenu.viewBGImage(event);"
                onclick="checkForMiddleClick(this, event);"/>
      <menuitem id="context-undo"
                data-l10n-id="text-action-undo"
                command="cmd_undo"/>
      <menuseparator id="context-sep-undo"/>
      <menuitem id="context-cut"
                data-l10n-id="text-action-cut"
                command="cmd_cut"/>
      <menuitem id="context-copy"
                data-l10n-id="text-action-copy"
                command="cmd_copy"/>
      <menuitem id="context-paste"
                data-l10n-id="text-action-paste"
                command="cmd_paste"/>
      <menuitem id="context-delete"
                data-l10n-id="text-action-delete"
                command="cmd_delete"/>
      <menuseparator id="context-sep-paste"/>
      <menuitem id="context-selectall"
                data-l10n-id="text-action-select-all"
                command="cmd_selectAll"/>
      <menuseparator id="context-sep-selectall"/>
      <menuitem id="context-keywordfield"
                data-l10n-id="main-context-menu-keyword"
                oncommand="AddKeywordForSearchField();"/>
      <menuitem id="context-searchselect"
                oncommand="BrowserSearch.loadSearchFromContext(this.searchTerms, this.principal, this.csp);"/>
      <menuseparator id="context-sep-sendlinktodevice" class="sync-ui-item"
                     hidden="true"/>
      <menu id="context-sendlinktodevice"
            class="sync-ui-item"
            data-l10n-id="main-context-menu-link-send-to-device"
            hidden="true">
        <menupopup id="context-sendlinktodevice-popup"
                   onpopupshowing="gSync.populateSendTabToDevicesMenu(event.target, gContextMenu.linkURL, gContextMenu.linkTextStr);"/>
      </menu>
      <menuseparator id="frame-sep"/>
      <menu id="frame" data-l10n-id="main-context-menu-frame">
        <menupopup>
          <menuitem id="context-showonlythisframe"
                    data-l10n-id="main-context-menu-frame-show-this"
                    oncommand="gContextMenu.showOnlyThisFrame();"/>
          <menuitem id="context-openframeintab"
                    data-l10n-id="main-context-menu-frame-open-tab"
                    oncommand="gContextMenu.openFrameInTab();"/>
          <menuitem id="context-openframe"
                    data-l10n-id="main-context-menu-frame-open-window"
                    oncommand="gContextMenu.openFrame();"/>
          <menuseparator id="open-frame-sep"/>
          <menuitem id="context-reloadframe"
                    data-l10n-id="main-context-menu-frame-reload"
                    oncommand="gContextMenu.reloadFrame(event);"/>
          <menuseparator/>
          <menuitem id="context-bookmarkframe"
                    data-l10n-id="main-context-menu-frame-bookmark"
                    oncommand="gContextMenu.addBookmarkForFrame();"/>
          <menuitem id="context-saveframe"
                    data-l10n-id="main-context-menu-frame-save-as"
                    oncommand="gContextMenu.saveFrame();"/>
          <menuseparator/>
          <menuitem id="context-printframe"
                    data-l10n-id="main-context-menu-frame-print"
                    oncommand="gContextMenu.printFrame();"/>
          <menuseparator/>
          <menuitem id="context-viewframesource"
                    data-l10n-id="main-context-menu-frame-view-source"
                    oncommand="gContextMenu.viewFrameSource();"/>
          <menuitem id="context-viewframeinfo"
                    data-l10n-id="main-context-menu-frame-view-info"
                    oncommand="gContextMenu.viewFrameInfo();"/>
        </menupopup>
      </menu>
      <menuitem id="context-viewpartialsource-selection"
                data-l10n-id="main-context-menu-view-selection-source"
                oncommand="gContextMenu.viewPartialSource();"/>
      <menuseparator id="context-sep-viewsource"/>
      <menuitem id="context-viewsource"
                data-l10n-id="main-context-menu-view-page-source"
                oncommand="BrowserViewSource(gContextMenu.browser);"/>
      <menuitem id="context-viewinfo"
                data-l10n-id="main-context-menu-view-page-info"
                oncommand="gContextMenu.viewInfo();"/>
      <menuseparator id="spell-separator"/>
      <menuitem id="spell-check-enabled"
                label="&spellCheckToggle.label;"
                type="checkbox"
                accesskey="&spellCheckToggle.accesskey;"
                oncommand="InlineSpellCheckerUI.toggleEnabled(window);"/>
      <menuitem id="spell-add-dictionaries-main"
                label="&spellAddDictionaries.label;"
                accesskey="&spellAddDictionaries.accesskey;"
                oncommand="gContextMenu.addDictionaries();"/>
      <menu id="spell-dictionaries"
            label="&spellDictionaries.label;"
            accesskey="&spellDictionaries.accesskey;">
          <menupopup id="spell-dictionaries-menu">
              <menuseparator id="spell-language-separator"/>
              <menuitem id="spell-add-dictionaries"
                        label="&spellAddDictionaries.label;"
                        accesskey="&spellAddDictionaries.accesskey;"
                        oncommand="gContextMenu.addDictionaries();"/>
          </menupopup>
      </menu>
      <menuseparator hidden="true" id="context-sep-bidi"/>
      <menuitem hidden="true" id="context-bidi-text-direction-toggle"
                data-l10n-id="main-context-menu-bidi-switch-text"
                command="cmd_switchTextDirection"/>
      <menuitem hidden="true" id="context-bidi-page-direction-toggle"
                data-l10n-id="main-context-menu-bidi-switch-page"
                oncommand="gContextMenu.switchPageDirection();"/>
      <menuseparator id="fill-login-separator" hidden="true"/>
      <menu id="fill-login"
            label="&fillLoginMenu.label;"
            label-login="&fillLoginMenu.label;"
            label-password="&fillPasswordMenu.label;"
            label-username="&fillUsernameMenu.label;"
            accesskey="&fillLoginMenu.accesskey;"
            accesskey-login="&fillLoginMenu.accesskey;"
            accesskey-password="&fillPasswordMenu.accesskey;"
            accesskey-username="&fillUsernameMenu.accesskey;"
            hidden="true">
        <menupopup id="fill-login-popup">
          <menuitem id="fill-login-no-logins"
                    label="&noLoginSuggestions.label;"
                    disabled="true"
                    hidden="true"/>
          <menuseparator id="generated-password-separator"/>
          <menuitem id="fill-login-generated-password"
                    label="&fillGeneratedPassword.label;"
                    accesskey="&fillGeneratedPassword.accesskey;"
                    hidden="true"
                    oncommand="gContextMenu.fillGeneratedPassword();"/>
          <menuseparator id="saved-logins-separator"/>
          <menuitem id="fill-login-saved-passwords"
                    label="&viewSavedLogins.label;"
                    oncommand="gContextMenu.openPasswordManager();"/>
        </menupopup>
      </menu>
      <menuseparator id="inspect-separator" hidden="true"/>
      <menuitem id="context-inspect"
                hidden="true"
                data-l10n-id="main-context-menu-inspect-element"
                oncommand="gContextMenu.inspectNode();"/>
      <menuitem id="context-inspect-a11y"
                hidden="true"
                data-l10n-id="main-context-menu-inspect-a11y-properties"
                oncommand="gContextMenu.inspectA11Y();"/>
