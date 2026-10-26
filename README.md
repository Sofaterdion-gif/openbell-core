# openbell-core

Deterministic market-scenario generation and trading math for simulations and games.
Extracted from [Open Bell](https://openbell.me), a trading/life-sim game by Tirde Studio.

C++17, header-only scenario engine + a tiny static library. Zero external dependencies.

## What it does

- **Scenario engine** (`openbell/scenario.hpp`): turns a handful of *narrative keypoints*
  ("day 15 closes at -20.5%") into full OHLC bar series. An AR(1) noise process is
  **pinned to zero at every keypoint**, so the generated path always hits the
  narrative extremes exactly — crash days crash, capitulation lows hold.
- **11 bundled historical scenarios** for education and playtesting: Black Monday 1987,
  the 2008 GFC, COVID-2020 (US/TW/HK), the 2021 meme-stock squeeze, the dot-com bust,
  the 2022 rate-hike bear market, the 1997 Asian crisis, the 2022 crypto winter, and more.
- **`.pack` file format**: author your own scenario packs in a plain-text key=value
  format (keypoint stubs or real OHLC CSV) — loaded at runtime from a `scenarios/`
  folder next to your executable. See [docs/PACK_FORMAT.md](docs/PACK_FORMAT.md).
- **Trading math** (`openbell/engine.hpp`): per-market maintenance-margin rates,
  liquidation prices, market-impact fill estimation (participation-capped square-root
  impact), account metrics.
- **Option pricing** (`openbell/pricing.hpp`): Black-Scholes European quotes with
  per-bar volatility, plus a realized-volatility estimator. Deterministic — no RNG.

Everything is seeded and deterministic: the same pack always produces the same bars,
which makes replay debugging, test oracles, and cross-engine parity checks trivial.

## Quick start

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
./build/openbell-demo        # (build\Release\openbell-demo.exe on Windows)
```

Or without CMake, header-only usage:

```bash
g++ -std=c++17 your_game.cpp -I include -o your_game   # scenario.hpp only
```

```cpp
#include <openbell/scenario.hpp>
#include <openbell/engine.hpp>
#include <openbell/pricing.hpp>
#include <cstdio>

int main() {
    // 1. Grab a bundled scenario and read its bars
    for (const auto& p : allPacks()) {
        if (p.id != "BLACK_MONDAY_US") continue;
        const auto& spx = p.bars[0];
        std::printf("crash day close = %.2f\n", spx[15].close);  // 225.00, every time
    }

    // 2. Liquidation price of a leveraged long
    double liq = trading::core::calculateLiquidationPrice(
        MarketMode::US, /*qty=*/10, /*avg=*/100.0, /*margin=*/500.0, /*short=*/false);

    // 3. Price a European call (bar-count time, per-bar vol)
    auto q = trading::core::priceEuropean(100.0, 100.0, /*T=*/20, /*sigma=*/0.01, true);
}
```

## Integrating into your project

**Option A — add_subdirectory / FetchContent** (recommended):

```cmake
FetchContent_Declare(openbell-core GIT_REPOSITORY https://github.com/Sofaterdion-gif/openbell-core.git GIT_TAG v0.1.0)
FetchContent_MakeAvailable(openbell-core)
target_link_libraries(your_target PRIVATE openbell-core::openbell-core)
```

**Option B — install once, then find_package**:

```bash
cmake -B build && cmake --install build --prefix /your/prefix
```

```cmake
find_package(openbell-core REQUIRED)
target_link_libraries(your_target PRIVATE openbell-core::openbell-core)
```

## Layout

```
include/openbell/
  market.hpp     MarketMode enum (US/TW/HK/Crypto/Funds/Futures)
  scenario.hpp   keypoints→OHLC engine, 11 bundled packs, .pack loader, CSV loader (header-only)
  engine.hpp     margin rates, liquidation price, market-impact fills, account metrics
  pricing.hpp    Black-Scholes European quotes + per-bar vol estimator
src/             engine.cpp, pricing.cpp
tests/           dependency-free test binaries (ctest)
examples/demo.cpp
docs/PACK_FORMAT.md
```

## Design notes

- **Determinism first**: `keysToBars` seeds `std::mt19937` with an FNV-1a hash of the
  pack/symbol id. Same id → same path, on every platform and every run.
- **Keypoint pinning**: the AR(1) deviation is subtracted back to exactly zero at every
  keypoint (linear interpolation between neighbors), so educational narrative beats
  are guaranteed, not statistical.
- **Approximate, educational data**: bundled packs compress months into tens of bars
  with approximate key prices. They teach the *shape* of events, not tick history.
- **Battle-tested by parity**: this core was validated in Open Bell by numeric parity
  tests against a reference SFML implementation and a Godot GDExtension port.

## Roadmap

- [ ] Warrant/option chain generation helpers (strikes around spot, quote chains)
- [ ] Python bindings for ML/quant experimentation
- [ ] More community scenario packs (contributions welcome!)
- [ ] Optional SQLite pack store

## License

MIT — see [LICENSE](LICENSE). Bundled scenario data is part of the code and covered
by the same license.

---

## 簡介（中文）

openbell-core 是從交易／人生模擬遊戲《Open Bell》（Tirde Studio）抽出的開源核心：
把「敘事關鍵點」（例如「第 15 天收盤跌 20.5%」）決定性地生成完整 K 線，內建 11 個
歷史事件情境包（黑色星期一、金融海嘯、COVID、迷因股軋空等），並提供保證金／清算價、
市場衝擊成本與 Black-Scholes 選擇權定價。C++17、零相依、跨平台，歡迎拿去用、拿去改。
