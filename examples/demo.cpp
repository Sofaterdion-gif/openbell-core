// openbell-core demo:
//   1. Load the bundled "Black Monday (1987)" scenario and print its SPX path —
//      note how the -20.5% crash day is hit exactly (keypoint pinning).
//   2. Price a European call with the Black-Scholes helper.
//   3. Compute a liquidation price and a market-impact fill estimate.
#include <openbell/engine.hpp>
#include <openbell/pricing.hpp>
#include <openbell/scenario.hpp>

#include <cstdio>

int main() {
    // 1. Scenario ----------------------------------------------------------------
    const ScenarioPack* blackMonday = nullptr;
    for (const auto& p : allPacks()) {
        if (p.id == "BLACK_MONDAY_US") blackMonday = &p;
    }
    if (!blackMonday) {
        std::fprintf(stderr, "BLACK_MONDAY_US pack not found\n");
        return 1;
    }
    std::printf("Pack: %s — %s (%s)\n", blackMonday->id.c_str(),
                blackMonday->title.c_str(), blackMonday->barNote.c_str());

    const auto& spx = blackMonday->bars[0];
    std::printf("SPX path (%zu bars):\n", spx.size());
    for (size_t d = 0; d < spx.size(); ++d) {
        std::printf("  day %2zu  O %7.2f  H %7.2f  L %7.2f  C %7.2f\n", d,
                    spx[d].open, spx[d].high, spx[d].low, spx[d].close);
    }

    // 2. Option pricing -----------------------------------------------------------
    const auto q = trading::core::priceEuropean(/*spot=*/100.0, /*strike=*/100.0,
                                                /*barsToExpiry=*/20, /*sigmaPerBar=*/0.01,
                                                /*isCall=*/true);
    std::printf("\nATM call (S=100, K=100, T=20 bars, sigma=1%%/bar):\n"
                "  price=%.4f  delta=%.4f  theta=%.6f  intrinsic=%.2f\n",
                q.price, q.delta, q.theta, q.intrinsic);

    // 3. Risk & fill math -----------------------------------------------------------
    const double liq = trading::core::calculateLiquidationPrice(
        MarketMode::US, /*qty=*/10, /*avg=*/100.0, /*margin=*/500.0, /*short=*/false);
    const auto fill = trading::core::estimateMarketFill(
        MarketMode::US, /*price=*/100.0, /*barHigh=*/101.0, /*barLow=*/99.0,
        /*qty=*/10000, /*buyerInitiated=*/true);
    std::printf("\nUS market: liquidation price of a 10-lot long @100 (margin 500) = %.2f\n", liq);
    std::printf("Market fill for 10000 sh buy @100: price=%.4f slippage=%.2f\n",
                fill.price, fill.slippageCost);

    std::printf("\nTotal packs available: %zu\n", allPacks().size());
    return 0;
}
