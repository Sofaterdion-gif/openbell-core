#include "openbell/engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace trading::core {

double maintenanceMarginRate(MarketMode market) {
    switch (market) {
        case MarketMode::US: return 0.25;
        case MarketMode::TW: return 0.30;
        case MarketMode::HK: return 0.25;
        case MarketMode::Crypto: return 0.20;
        case MarketMode::Funds: return 1.0;
        case MarketMode::Futures: return 0.05;
        default: return 1.0;
    }
}

double calculateLiquidationPrice(MarketMode market, int quantity, double averagePrice,
                                 double margin, bool isShort, double closingFeeRate) {
    if (quantity <= 0 || !std::isfinite(averagePrice) || averagePrice <= 0.0 ||
        !std::isfinite(margin) || margin < 0.0 ||
        !std::isfinite(closingFeeRate) || closingFeeRate < 0.0)
        return 0.0;

    const double maintenanceRate = maintenanceMarginRate(market);
    const double marginPerUnit = margin / quantity;
    if (isShort)
        return (averagePrice + marginPerUnit) / (1.0 + maintenanceRate + closingFeeRate);

    const double denominator = 1.0 - maintenanceRate - closingFeeRate;
    if (denominator <= 0.0) return 0.0;
    return std::max(0.0, (averagePrice - marginPerUnit) / denominator);
}

double estimatedBarCapacity(MarketMode market) {
    switch (market) {
        case MarketMode::US: return 5000.0;
        case MarketMode::TW: return 2000.0;
        case MarketMode::HK: return 5000.0;
        case MarketMode::Crypto: return 2500.0;
        case MarketMode::Funds: return 5000.0;
        case MarketMode::Futures: return 5.0;
        default: return 1000.0;
    }
}

std::vector<MarketCandle> generateDemoCandles(int count) {
    std::vector<MarketCandle> candles;
    if (count <= 0) return candles;
    candles.reserve(static_cast<std::size_t>(count));

    double basePrice = 18000.0;
    for (int i = 1; i <= count; ++i) {
        const double open = basePrice + (i % 10 - 5) * 10.0;
        const double high = open + 25.0;
        const double low = open - 20.0;
        const double close = open + (i % 3 - 1) * 15.0;
        const double volume = 150000.0 + (i * 123) % 50000;
        candles.push_back({i, open, high, low, close, volume});
        basePrice = close;
    }
    return candles;
}

MarketFill estimateMarketFill(MarketMode market, double price, double barHigh,
                              double barLow, int quantity, bool buyerInitiated) {
    MarketFill fill{price, 0.0};
    if (quantity <= 0 || !std::isfinite(price) || price <= 0.0)
        return fill;

    const double participation = quantity / estimatedBarCapacity(market);
    if (participation <= 0.01)
        return fill;

    const double impactRate = std::min(0.005, 0.00002 + 0.0005 * std::sqrt(participation));
    const double barRange = std::isfinite(barHigh) && std::isfinite(barLow)
        ? std::max(0.0, barHigh - barLow)
        : 0.0;
    const double rangeLimit = barRange > 0.0 ? barRange * 0.25 : price * 0.01;
    const double impact = std::min(price * impactRate, rangeLimit);
    fill.price = std::max(0.01, price + (buyerInitiated ? impact : -impact));
    fill.slippageCost = impact * quantity;
    return fill;
}

AccountMetrics calculateAccountMetrics(MarketMode market, double netWorth,
                                       const std::vector<PositionSnapshot>& positions) {
    AccountMetrics metrics;
    for (const PositionSnapshot& position : positions) {
        const double longQuantity = std::max(0, position.longQuantity);
        const double shortQuantity = std::max(0, position.shortQuantity);
        metrics.unrealizedPnl +=
            (position.price - position.longAverage) * longQuantity +
            (position.shortAverage - position.price) * shortQuantity;
        metrics.grossExposure += position.price * (longQuantity + shortQuantity);
        metrics.usedMargin += position.longMargin + position.shortMargin;
    }
    metrics.maintenanceRequirement =
        metrics.grossExposure * maintenanceMarginRate(market);
    metrics.availableMargin = netWorth - metrics.usedMargin;
    metrics.leverageRatio = netWorth > 0.0 ? metrics.grossExposure / netWorth : 0.0;
    return metrics;
}

}  // namespace trading::core
