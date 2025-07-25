/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

  await Services.search.addEngineWithDetails("SearchEngine", {
    method: "GET",
    template: "http://s.example.com/search",
  });
  let engine = Services.search.getEngineByName("SearchEngine");
  engine.addParam("q", "{searchTerms}", null);
  registerCleanupFunction(async () => Services.search.removeEngine(engine));

  let uri1 = NetUtil.newURI("http://s.example.com/search?q=Terms&client=1");
  let uri2 = NetUtil.newURI("http://s.example.com/search?q=Terms&client=2");
  await PlacesTestUtils.addVisits({
    uri: uri1,
    title: "Terms - SearchEngine Search",
  });
  await addBookmark({ uri: uri2, title: "Terms - SearchEngine Search" });

  info("Past search terms should be styled, unless bookmarked");
  Services.prefs.setBoolPref("browser.urlbar.restyleSearches", true);
  await check_autocomplete({
    search: "term",
    matches: [
      makeSearchMatch("Terms", {
        engineName: "SearchEngine",
        style: ["favicon"],
      }),
      {
        uri: uri2,
        title: "Terms - SearchEngine Search",
        style: ["bookmark"],
      },
    ],
  });

  info("Past search terms should not be styled if restyling is disabled");
  Services.prefs.setBoolPref("browser.urlbar.restyleSearches", false);
  await check_autocomplete({
    search: "term",
    matches: [
      { uri: uri1, title: "Terms - SearchEngine Search" },
      { uri: uri2, title: "Terms - SearchEngine Search", style: ["bookmark"] },
    ],
  });

  await cleanup();
});
