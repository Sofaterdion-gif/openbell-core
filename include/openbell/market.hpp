#pragma once

namespace openbell {

// Market regime used by margin rules, bar-capacity (liquidity) tables and the
// bundled scenario packs. Kept outside the trading::core namespace for
// drop-in compatibility with the original Open Bell sources.
enum class MarketMode { US = 0, TW, HK, Crypto, Funds, Futures, COUNT };

}  // namespace openbell

// Back-compat alias: the original codebase refers to MarketMode unqualified.
using openbell::MarketMode;
