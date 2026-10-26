#include "check.hpp"

#include <openbell/pricing.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

void testAtmCallBounds() {
    // ATM call ≈ 0.4 * S * sigma * sqrt(T) = 0.4 * 100 * 0.01 * sqrt(20) ≈ 1.789
    const auto q = trading::core::priceEuropean(100.0, 100.0, 20.0, 0.01, true);
    OB_CHECK(q.price > 1.5 && q.price < 2.0);
    OB_CHECK(std::abs(q.delta - 0.5) < 0.05);
    OB_CHECK(q.theta < 0.0);  // long options bleed time value
}

void testPutCallParityWithZeroRate() {
    // r = 0 → C - P = S - K
    const double S = 100.0, K = 95.0, T = 20.0, sigma = 0.01;
    const auto call = trading::core::priceEuropean(S, K, T, sigma, true);
    const auto put = trading::core::priceEuropean(S, K, T, sigma, false);
    OB_CHECK(std::abs((call.price - put.price) - (S - K)) < 1e-10);
}

void testExpiryReturnsIntrinsic() {
    const auto call = trading::core::priceEuropean(120.0, 100.0, 0.0, 0.01, true);
    OB_CHECK(call.price == 20.0);
    OB_CHECK(call.delta == 1.0);

    const auto put = trading::core::priceEuropean(120.0, 100.0, 0.0, 0.01, false);
    OB_CHECK(put.price == 0.0);
    OB_CHECK(put.delta == 0.0);
}

void testSigmaFloorOnFlatSeries() {
    const std::vector<double> flat(30, 100.0);
    const double sigma = trading::core::estimateSigmaPerBar(flat, 29, 20);
    OB_CHECK(sigma >= 0.004);
}

void testSigmaFromSyntheticVaryingReturns() {
    // Alternating ±1% log returns: mean 0, E[r^2] = 1e-4 → per-bar sigma = 0.01 exactly.
    std::vector<double> closes(101);
    closes[0] = 100.0;
    for (size_t i = 1; i < closes.size(); ++i)
        closes[i] = closes[i - 1] * std::exp((i % 2 == 0) ? 0.01 : -0.01);
    const double sigma = trading::core::estimateSigmaPerBar(closes, 100, 50);
    OB_CHECK(std::abs(sigma - 0.01) < 1e-12);
}

}  // namespace

int main() {
    testAtmCallBounds();
    testPutCallParityWithZeroRate();
    testExpiryReturnsIntrinsic();
    testSigmaFloorOnFlatSeries();
    testSigmaFromSyntheticVaryingReturns();
    std::puts("test_pricing: ALL PASSED");
}
