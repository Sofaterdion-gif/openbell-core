#pragma once

#include <cstddef>
#include <vector>

namespace trading::core {

// Black-Scholes（歐式、無風險利率 0）。時間單位是「K 線根數」，波動率是每根 K 線的對數報酬標準差。
// 權證／期權玩法專用；不使用亂數，與行情產生器無關。
struct OptionQuote {
    double price = 0.0;      // 每單位（對應 1 股標的）的價格
    double delta = 0.0;      // 標的變動 1 元時，權證價格約變動多少
    double theta = 0.0;      // 每過 1 根 K 線損失的時間價值（通常為負）
    double intrinsic = 0.0;  // 內在價值 max(S-K, 0) 或 max(K-S, 0)
    double sigma = 0.0;      // 使用的每根波動率
};

double normalCdf(double x);
double normalPdf(double x);

// barsToExpiry <= 0 時回傳內在價值（delta 為 0 或 ±1）。
OptionQuote priceEuropean(double spot, double strike, double barsToExpiry,
                          double sigmaPerBar, bool isCall);

// 以最近 window 根收盤價的對數報酬標準差估計每根波動率；樣本不足或太小時使用下限。
double estimateSigmaPerBar(const std::vector<double>& closes, size_t endIndex,
                           size_t window = 20);

}  // namespace trading::core
