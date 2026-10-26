#include "check.hpp"

#include <openbell/engine.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

bool closeEnough(double actual, double expected, double tolerance = 1e-8) {
    return std::abs(actual - expected) <= tolerance;
}

void testLiquidationPriceIncludesClosingFee() {
    const double withoutFee = trading::core::calculateLiquidationPrice(
        MarketMode::US, 10, 100.0, 500.0, false);
    const double withFee = trading::core::calculateLiquidationPrice(
        MarketMode::US, 10, 100.0, 500.0, false, 0.001);

    OB_CHECK(closeEnough(withoutFee, 66.66666666666667));
    OB_CHECK(withFee > withoutFee);
    OB_CHECK(trading::core::calculateLiquidationPrice(
        MarketMode::US, 0, 100.0, 500.0, false) == 0.0);
}

void testMaintenanceMarginRates() {
    OB_CHECK(trading::core::maintenanceMarginRate(MarketMode::US) == 0.25);
    OB_CHECK(trading::core::maintenanceMarginRate(MarketMode::TW) == 0.30);
    OB_CHECK(trading::core::maintenanceMarginRate(MarketMode::Futures) == 0.05);
    OB_CHECK(trading::core::maintenanceMarginRate(MarketMode::Funds) == 1.0);
}

void testDemoCandlesAreConsistentAndValid() {
    const auto candles = trading::core::generateDemoCandles();
    OB_CHECK(candles.size() == 504);
    OB_CHECK(candles.front().timeIndex == 1);
    OB_CHECK(candles.back().timeIndex == 504);
    OB_CHECK(closeEnough(candles.front().open, 17960.0));
    OB_CHECK(closeEnough(candles.front().close, 17960.0));
    for (const auto& candle : candles) {
        OB_CHECK(candle.high >= candle.open);
        OB_CHECK(candle.high >= candle.close);
        OB_CHECK(candle.low <= candle.open);
        OB_CHECK(candle.low <= candle.close);
        OB_CHECK(candle.volume >= 0.0);
    }
    OB_CHECK(trading::core::generateDemoCandles(0).empty());
}

void testMarketFillDirectionAndSize() {
    const auto noImpact = trading::core::estimateMarketFill(
        MarketMode::US, 100.0, 101.0, 99.0, 10, true);
    const auto buy = trading::core::estimateMarketFill(
        MarketMode::US, 100.0, 101.0, 99.0, 10000, true);
    const auto sell = trading::core::estimateMarketFill(
        MarketMode::US, 100.0, 101.0, 99.0, 10000, false);

    OB_CHECK(closeEnough(noImpact.price, 100.0));
    OB_CHECK(buy.price > 100.0);
    OB_CHECK(sell.price < 100.0);
    OB_CHECK(buy.slippageCost > 0.0);
    OB_CHECK(closeEnough(buy.slippageCost, (buy.price - 100.0) * 10000.0));
}

void testAccountMetrics() {
    const std::vector<trading::core::PositionSnapshot> positions{
        {110.0, 100.0, 0.0, 500.0, 0.0, 10, 0},
        {50.0, 0.0, 60.0, 0.0, 200.0, 0, 4}
    };
    const auto metrics = trading::core::calculateAccountMetrics(
        MarketMode::US, 1200.0, positions);

    OB_CHECK(closeEnough(metrics.unrealizedPnl, 140.0));
    OB_CHECK(closeEnough(metrics.grossExposure, 1300.0));
    OB_CHECK(closeEnough(metrics.usedMargin, 700.0));
    OB_CHECK(closeEnough(metrics.maintenanceRequirement, 325.0));
    OB_CHECK(closeEnough(metrics.availableMargin, 500.0));
    OB_CHECK(closeEnough(metrics.leverageRatio, 1300.0 / 1200.0));
}

}  // namespace

int main() {
    testLiquidationPriceIncludesClosingFee();
    testMaintenanceMarginRates();
    testDemoCandlesAreConsistentAndValid();
    testMarketFillDirectionAndSize();
    testAccountMetrics();
    std::puts("test_engine: ALL PASSED");
}
