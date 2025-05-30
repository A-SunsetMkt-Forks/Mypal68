/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "signaling/src/sdp/HybridSdpParser.h"
#include "signaling/src/sdp/SdpLog.h"
#include "signaling/src/sdp/SdpPref.h"
#include "signaling/src/sdp/SipccSdpParser.h"
#include "signaling/src/sdp/RsdparsaSdpParser.h"
#include "signaling/src/sdp/ParsingResultComparer.h"

#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"

#include <unordered_map>

namespace mozilla {

using mozilla::LogLevel;

const std::string& HybridSdpParser::ParserName() {
  const static std::string PARSER_NAME = "hybrid";
  return PARSER_NAME;
}

HybridSdpParser::HybridSdpParser()
    : mStrictSuccess(SdpPref::StrictSuccess()),
      mPrimary(SdpPref::Primary()),
      mSecondary(SdpPref::Secondary()),
      mFailover(SdpPref::Failover()) {
  MOZ_ASSERT(!(mSecondary && mFailover),
             "Can not have both a secondary and failover parser!");
  MOZ_LOG(SdpLog, LogLevel::Info,
          ("Primary SDP Parser: %s", mPrimary->Name().c_str()));
  mSecondary.apply([](auto& parser) {
    MOZ_LOG(SdpLog, LogLevel::Info,
            ("Secondary SDP Logger: %s", parser->Name().c_str()));
  });
  mFailover.apply([](auto& parser) {
    MOZ_LOG(SdpLog, LogLevel::Info,
            ("Failover SDP Logger: %s", parser->Name().c_str()));
  });
}

auto HybridSdpParser::Parse(const std::string& aText)
    -> UniquePtr<SdpParser::Results> {
  using Results = UniquePtr<SdpParser::Results>;
  auto results = mPrimary->Parse(aText);
  auto successful = [&](Results& aRes) -> bool {
    // In strict mode any reported error counts as failure
    if (mStrictSuccess) {
      return aRes->Ok();
    }
    return aRes->Sdp() != nullptr;
  };
  // Pass results on for comparison and return A if it was a success and B
  // otherwise.
  auto compare = [&](Results&& aResB) -> Results {
    ParsingResultComparer::Compare(results, aResB, aText);
    return std::move(successful(results) ? results : aResB);
  };
  // Run secondary parser, if there is one, and update selected results.
  mSecondary.apply(
      [&](auto& sec) { results = compare(std::move(sec->Parse(aText))); });
  // Run failover parser, if there is one, and update selected results.
  mFailover.apply([&](auto& failover) {  // Only run if primary parser failed
    if (!successful(results)) {
      results = compare(std::move(failover->Parse(aText)));
    }
  });
  return results;
}

}  // namespace mozilla
