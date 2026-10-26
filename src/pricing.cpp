#include "openbell/pricing.hpp"

#include <algorithm>
#include <cmath>

namespace trading::core {

namespace {
constexpr double kSigmaFloor = 0.004;
constexpr double kInvSqrt2Pi = 0.3989422804014327;
}  // namespace

double normalCdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

double normalPdf(double x) {
    return kInvSqrt2Pi * std::exp(-0.5 * x * x);
}

OptionQuote priceEuropean(double spot, double strike, double barsToExpiry,
                          double sigmaPerBar, bool isCall) {
    OptionQuote quote;
    spot = std::max(spot, 1e-9);
    strike = std::max(strike, 1e-9);
    quote.intrinsic = isCall ? std::max(spot - strike, 0.0) : std::max(strike - spot, 0.0);
    quote.sigma = std::max(sigmaPerBar, kSigmaFloor);
    if (barsToExpiry <= 0.0) {
        quote.price = quote.intrinsic;
        quote.delta = isCall ? (spot > strike ? 1.0 : 0.0) : (spot < strike ? -1.0 : 0.0);
        return quote;
    }
    const double total_sigma = quote.sigma * std::sqrt(barsToExpiry);
    const double d1 = (std::log(spot / strike) + 0.5 * total_sigma * total_sigma) / total_sigma;
    const double d2 = d1 - total_sigma;
    if (isCall) {
        quote.price = spot * normalCdf(d1) - strike * normalCdf(d2);
        quote.delta = normalCdf(d1);
    } else {
        quote.price = strike * normalCdf(-d2) - spot * normalCdf(-d1);
        quote.delta = normalCdf(d1) - 1.0;
    }
    quote.price = std::max(quote.price, quote.intrinsic);
    quote.theta = -(spot * normalPdf(d1) * quote.sigma) / (2.0 * std::sqrt(barsToExpiry));
    return quote;
}

double estimateSigmaPerBar(const std::vector<double>& closes, size_t endIndex, size_t window) {
    if (closes.empty())
        return kSigmaFloor;
    endIndex = std::min(endIndex, closes.size() - 1);
    const size_t first = endIndex >= window ? endIndex - window + 1 : 1;
    double sum = 0.0;
    double sum_squares = 0.0;
    int count = 0;
    for (size_t i = std::max<size_t>(first, 1); i <= endIndex; ++i) {
        if (closes[i - 1] <= 0.0 || closes[i] <= 0.0)
            continue;
        const double r = std::log(closes[i] / closes[i - 1]);
        sum += r;
        sum_squares += r * r;
        ++count;
    }
    if (count < 3)
        return kSigmaFloor * 2.0;
    const double mean = sum / count;
    const double variance = std::max(sum_squares / count - mean * mean, 0.0);
    return std::max(std::sqrt(variance), kSigmaFloor);
}

}  // namespace trading::core
