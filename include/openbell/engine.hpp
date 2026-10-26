#pragma once

#include "market.hpp"

#include <vector>

namespace trading::core {

struct MarketFill {
    double price = 0.0;
    double slippageCost = 0.0;
};

struct MarketCandle {
    int timeIndex = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
};

struct PositionSnapshot {
    double price = 0.0;
    double longAverage = 0.0;
    double shortAverage = 0.0;
    double longMargin = 0.0;
    double shortMargin = 0.0;
    int longQuantity = 0;
    int shortQuantity = 0;
};

struct AccountMetrics {
    double unrealizedPnl = 0.0;
    double grossExposure = 0.0;
    double usedMargin = 0.0;
    double maintenanceRequirement = 0.0;
    double availableMargin = 0.0;
    double leverageRatio = 0.0;
};

double maintenanceMarginRate(MarketMode market);
double calculateLiquidationPrice(MarketMode market, int quantity, double averagePrice,
                                 double margin, bool isShort, double closingFeeRate = 0.0);
double estimatedBarCapacity(MarketMode market);
std::vector<MarketCandle> generateDemoCandles(int count = 504);
MarketFill estimateMarketFill(MarketMode market, double price, double barHigh,
                              double barLow, int quantity, bool buyerInitiated);
AccountMetrics calculateAccountMetrics(MarketMode market, double netWorth,
                                       const std::vector<PositionSnapshot>& positions);

}  // namespace trading::core
