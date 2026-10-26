// scenario.hpp — 經典情境包（教學示意 stub）
//
// 所有價格都是「近似關鍵點 + 決定性雜訊」生成的示意 OHLC，用來呈現事件的敘事形狀
// （急跌、反彈、陰跌、軋空…），不是真實逐筆或逐日資料。每根 K 棒可能代表 1~數個交易日。
//
// 內建包嵌入在本檔；另外會在執行時讀取 scenarios/*.pack（格式見 docs/PACK_FORMAT.md）。
#pragma once

#include "market.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // keep windows.h from defining min/max macros that break std::min/std::max
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
#include <vector>

constexpr double PACK_PRICE_FLOOR = 0.01;

struct OHLC {
    double open = 0, high = 0, low = 0, close = 0;
    std::string timestamp;
};

struct ScenarioPack {
    std::string id;
    std::string title;
    std::string blurb;          // may contain '\n'
    std::string barNote;        // e.g. "每根≈1 交易日"
    MarketMode market = MarketMode::US;
    std::string currency;
    double startCash = 10000.0;
    std::vector<std::string> symbols;   // max 3 used by UI
    std::vector<std::vector<OHLC>> bars; // bars[symbol][day]
    std::string source = "內建";         // "內建" or file name
};

// One narrative keypoint: day index, close, optional absolute high/low override (0 = auto).
struct KeyPt {
    int day = 0;
    double close = 0;
    double high = 0;
    double low = 0;
};

struct KeySeries {
    std::string symbol;
    double noise = 0.02;  // daily noise scale (log-space sigma)
    std::vector<KeyPt> keys;
};

inline std::uint32_t packHash(const std::string& s) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// Keypoints → OHLC. Log-linear interpolation between keypoints, plus AR(1) noise that is
// pinned to zero at every keypoint (so the narrative extremes are hit exactly).
inline std::vector<OHLC> keysToBars(const std::string& seedKey, std::vector<KeyPt> keys, double noise, int totalDays) {
    std::vector<OHLC> out;
    if (keys.empty()) return out;
    std::sort(keys.begin(), keys.end(), [](const KeyPt& a, const KeyPt& b) { return a.day < b.day; });
    if (keys.front().day != 0) keys.insert(keys.begin(), KeyPt{0, keys.front().close, 0, 0});
    if (keys.back().day < totalDays - 1) keys.push_back(KeyPt{totalDays - 1, keys.back().close, 0, 0});
    int n = keys.back().day + 1;

    std::vector<double> base(n, keys.front().close);
    std::vector<double> hiOv(n, 0), loOv(n, 0);
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        int d0 = keys[k].day, d1 = keys[k + 1].day;
        double l0 = std::log(std::max(PACK_PRICE_FLOOR, keys[k].close));
        double l1 = std::log(std::max(PACK_PRICE_FLOOR, keys[k + 1].close));
        for (int d = d0; d <= d1 && d < n; ++d) {
            double t = (d1 == d0) ? 0.0 : double(d - d0) / double(d1 - d0);
            base[d] = std::exp(l0 * (1 - t) + l1 * t);
        }
    }
    for (const auto& k : keys) {
        if (k.day >= 0 && k.day < n) {
            hiOv[k.day] = k.high;
            loOv[k.day] = k.low;
        }
    }

    std::mt19937 rng(packHash(seedKey));
    std::normal_distribution<double> nd(0.0, 1.0);
    std::vector<double> e(n, 0.0);
    double prev = 0.0;
    for (int d = 0; d < n; ++d) {
        prev = 0.55 * prev + nd(rng) * noise;
        e[d] = prev;
    }
    std::vector<double> dev(n, 0.0);
    for (size_t k = 0; k + 1 < keys.size(); ++k) {
        int d0 = keys[k].day, d1 = keys[k + 1].day;
        for (int d = d0; d <= d1 && d < n; ++d) {
            double t = (d1 == d0) ? 0.0 : double(d - d0) / double(d1 - d0);
            dev[d] = e[d] - (e[d0] * (1 - t) + e[d1] * t);
        }
    }

    double prevClose = base[0] * std::exp(-std::abs(nd(rng)) * noise * 0.3);
    for (int d = 0; d < n; ++d) {
        double c = std::max(PACK_PRICE_FLOOR, base[d] * std::exp(dev[d]));
        double o = std::max(PACK_PRICE_FLOOR, prevClose * std::exp(nd(rng) * noise * 0.25));
        double move = std::abs(std::log(c / o));
        double hi = std::max(o, c) * std::exp(std::abs(nd(rng)) * noise * 0.6 + 0.12 * move);
        double lo = std::min(o, c) * std::exp(-(std::abs(nd(rng)) * noise * 0.6 + 0.12 * move));
        if (hiOv[d] > 0) hi = std::max(hiOv[d], std::max(o, c));
        if (loOv[d] > 0) lo = std::min(loOv[d], std::min(o, c));
        lo = std::max(PACK_PRICE_FLOOR, lo);
        if (hi < lo) hi = lo;
        out.push_back({o, hi, lo, c, ""});
        prevClose = c;
    }
    return out;
}

inline ScenarioPack buildPack(const std::string& id, const std::string& title, const std::string& blurb,
                              const std::string& barNote, MarketMode m, const std::string& ccy, double cash,
                              const std::vector<KeySeries>& series) {
    ScenarioPack p;
    p.id = id;
    p.title = title;
    p.blurb = blurb;
    p.barNote = barNote;
    p.market = m;
    p.currency = ccy;
    p.startCash = cash;
    int total = 1;
    for (const auto& s : series)
        for (const auto& k : s.keys) total = std::max(total, k.day + 1);
    for (const auto& s : series) {
        p.symbols.push_back(s.symbol);
        p.bars.push_back(keysToBars(id + "/" + s.symbol, s.keys, s.noise, total));
    }
    return p;
}

// ---------------------------------------------------------------- embedded packs

inline ScenarioPack packCovidUs() {
    return buildPack(
        "COVID_US", "COVID 美股急跌反彈（2020）",
        "2020/2/19 高點 → 3/23 低點（S&P 約 -34%）→ 4 月 V 形反彈。\n"
        "TSLA 波動最大；AAPL/TSLA 為分割前價位。關鍵點為近似值。",
        "每根≈1 交易日，約 45 日", MarketMode::US, "USD", 10000.0,
        {
            {"SPY", 0.012, {{0, 332}, {6, 338}, {10, 313}, {13, 297}, {16, 300}, {19, 274}, {22, 240},
                            {24, 250}, {27, 222, 0, 218}, {30, 261}, {34, 250}, {38, 278}, {42, 285}, {44, 290}}},
            {"AAPL", 0.018, {{0, 320}, {6, 324}, {10, 292}, {13, 274}, {16, 289}, {19, 249}, {22, 242},
                             {24, 247}, {27, 224, 0, 212}, {30, 258}, {34, 241}, {38, 267}, {42, 287}, {44, 285}}},
            {"TSLA", 0.035, {{0, 770}, {3, 900, 968}, {6, 917}, {10, 780}, {13, 640}, {16, 700}, {19, 500},
                             {22, 430}, {24, 450}, {27, 361, 0, 350}, {30, 500}, {34, 480}, {38, 550},
                             {42, 720}, {44, 710}}},
        });
}

inline ScenarioPack packCryptoWinter() {
    return buildPack(
        "CRYPTO_WINTER_2022", "加密寒冬（2022）",
        "4 月高檔 → 5 月 LUNA 崩盤 → 6 月投降式低點 → 夏季反彈 →\n"
        "11 月 FTX 爆雷再破底。SOL 受 FTX 打擊最重。關鍵點為近似值。",
        "每根≈4~6 日，壓縮約 8 個月", MarketMode::Crypto, "USDT", 10000.0,
        {
            {"BTC", 0.03, {{0, 47000}, {5, 42000}, {9, 38500}, {12, 31000, 0, 26700}, {14, 29000}, {18, 30000},
                           {22, 29500}, {25, 22500}, {27, 18500}, {28, 17600, 0, 17500}, {31, 20500}, {35, 21000},
                           {39, 24000}, {43, 20000}, {47, 19200}, {51, 20500}, {53, 21000},
                           {55, 16500, 0, 15700}, {57, 16000}, {59, 16800}}},
            {"ETH", 0.04, {{0, 3500}, {5, 3000}, {9, 2800}, {12, 2000}, {14, 1950}, {18, 1950}, {22, 1800},
                           {25, 1200}, {28, 900, 0, 880}, {31, 1100}, {35, 1200}, {39, 1950}, {43, 1550},
                           {47, 1300}, {51, 1350}, {53, 1600}, {55, 1200, 0, 1080}, {57, 1150}, {59, 1200}}},
            {"SOL", 0.055, {{0, 135}, {5, 100}, {9, 85}, {12, 50}, {14, 45}, {18, 50}, {22, 40}, {25, 30},
                            {28, 28}, {31, 36}, {35, 38}, {39, 45}, {43, 33}, {47, 32}, {51, 32}, {53, 35},
                            {55, 14, 0, 12}, {57, 12}, {59, 13}}},
        });
}

inline ScenarioPack pack2008Us() {
    return buildPack(
        "2008_US", "金融海嘯 GFC（2008-09）",
        "2008/9 雷曼倒閉 → TARP 爭議 → 10 月恐慌（含 +11% 單日反彈）→\n"
        "11 月低點 → 2009/3/9 最終底部後反彈。花旗(C)跌近 95%。關鍵點為近似值。",
        "每根≈2~3 交易日，壓縮約 6 個月", MarketMode::US, "USD", 10000.0,
        {
            {"SPY", 0.018, {{0, 128}, {4, 126}, {6, 120}, {7, 115}, {9, 121}, {12, 110}, {14, 100}, {16, 90},
                            {18, 88, 0, 84}, {19, 98}, {20, 95}, {22, 87}, {25, 96}, {28, 93}, {30, 85},
                            {33, 76, 0, 74}, {36, 88}, {40, 90}, {44, 92}, {48, 83}, {51, 84}, {54, 75},
                            {57, 68, 0, 66}, {59, 76}}},
            {"C", 0.06, {{0, 19}, {6, 17}, {7, 15}, {9, 20}, {12, 17}, {16, 13}, {19, 16}, {22, 13}, {28, 14},
                         {32, 8}, {33, 4.7, 0, 3.8}, {34, 5.9}, {36, 8}, {40, 7.5}, {44, 6.5}, {48, 3.6},
                         {51, 3.0}, {54, 2.0}, {57, 1.05, 0, 0.97}, {59, 2.6}}},
            {"GS", 0.045, {{0, 165}, {6, 135}, {7, 125}, {9, 140}, {12, 120}, {16, 95}, {19, 115}, {22, 100},
                           {28, 85}, {33, 55, 0, 47}, {36, 75}, {40, 80}, {44, 85}, {48, 75}, {52, 78},
                           {57, 60}, {59, 90}}},
        });
}

inline ScenarioPack packBlackMonday() {
    return buildPack(
        "BLACK_MONDAY_US", "黑色星期一（1987）",
        "9 月下旬緩跌 → 10/16 週五 -5% → 10/19 單日 -20.5%（史上最大單日跌幅）→\n"
        "隔週劇烈震盪、10/26 再跌。短而急，考驗停損與抄底。關鍵點為近似值。",
        "每根≈1 交易日，約 30 日", MarketMode::US, "USD", 10000.0,
        {
            {"SPX", 0.009, {{0, 320}, {3, 318}, {6, 328}, {8, 321}, {10, 314}, {12, 305}, {13, 298}, {14, 282},
                            {15, 225, 282.7, 221}, {16, 236, 245, 216}, {17, 258}, {18, 248}, {19, 248},
                            {20, 233, 0, 227}, {21, 237}, {22, 245}, {23, 244}, {24, 251}, {25, 255},
                            {26, 250}, {27, 248}, {28, 245}, {29, 250}}},
            {"IBM", 0.014, {{0, 150}, {6, 150}, {10, 140}, {13, 138}, {14, 135}, {15, 104, 134, 102}, {16, 110},
                            {17, 120}, {18, 117}, {20, 105}, {23, 112}, {26, 115}, {29, 117}}},
            {"XOM", 0.014, {{0, 48}, {6, 49}, {10, 47}, {13, 45}, {14, 43}, {15, 34, 43, 32}, {16, 36},
                            {17, 39}, {18, 38}, {20, 35}, {23, 37}, {26, 37}, {29, 38}}},
        });
}

inline ScenarioPack packCovidTw() {
    return buildPack(
        "COVID_TW", "COVID 台股急跌反彈（2020）",
        "2 月高檔 → 3/19 加權指數約 8500 低點 → 4 月起反彈。\n"
        "台積電(2330)、鴻海(2317)、元大台灣50(0050)。以「股」為單位（非張）。",
        "每根≈1 交易日，約 45 日", MarketMode::TW, "TWD", 300000.0,
        {
            {"2330", 0.015, {{0, 320}, {5, 333}, {10, 316}, {13, 305}, {16, 300}, {19, 280}, {22, 260}, {24, 270},
                             {26, 248, 0, 235}, {29, 268}, {33, 265}, {37, 280}, {41, 290}, {44, 297}}},
            {"2317", 0.018, {{0, 82}, {5, 80}, {10, 76}, {13, 73}, {16, 72}, {19, 68}, {22, 63}, {24, 66},
                             {26, 60, 0, 59.6}, {29, 67}, {33, 68}, {37, 72}, {41, 74}, {44, 75}}},
            {"0050", 0.012, {{0, 95}, {5, 97}, {10, 92}, {13, 89}, {16, 88}, {19, 81}, {22, 75}, {24, 77},
                             {26, 67.3, 0, 66}, {29, 76}, {33, 75}, {37, 79}, {41, 82}, {44, 84}}},
        });
}

inline ScenarioPack packCovidHk() {
    return buildPack(
        "COVID_HK", "COVID 港股（2020）",
        "騰訊(0700)相對抗跌；匯豐(0005)在 3 月低點後又因 4 月取消派息再挫；\n"
        "盈富基金(2800)代表大盤。反彈力道明顯弱於美股。關鍵點為近似值。",
        "每根≈1 交易日，約 45 日", MarketMode::HK, "HKD", 80000.0,
        {
            {"0700", 0.016, {{0, 410}, {5, 418}, {10, 395}, {13, 380}, {16, 390}, {19, 365}, {22, 350}, {24, 360},
                             {26, 345, 0, 335}, {29, 380}, {33, 375}, {37, 385}, {41, 392}, {44, 398}}},
            {"0005", 0.016, {{0, 56}, {5, 55}, {10, 52}, {13, 48}, {16, 47}, {19, 42}, {22, 40}, {24, 41},
                             {26, 37.5}, {29, 41}, {33, 40}, {37, 40}, {38, 36, 0, 35}, {41, 37}, {44, 38}}},
            {"2800", 0.011, {{0, 27.5}, {5, 27.8}, {10, 26.5}, {13, 25.5}, {16, 25.8}, {19, 23.5}, {22, 22.4},
                             {24, 23.0}, {26, 21.5, 0, 21.2}, {29, 23.5}, {33, 23.2}, {37, 23.8}, {41, 24.3},
                             {44, 24.6}}},
        });
}

inline ScenarioPack packMeme2021() {
    return buildPack(
        "MEME_2021_US", "迷因股軋空（2021）",
        "1 月 GME 從 ~$17 被散戶軋空到盤中 $483（1/28 券商限制買入當日巨幅長影線），\n"
        "2 月崩回 $40，2 月底二次噴發。AMC、BB 同步暴漲暴跌。關鍵點為近似值。",
        "每根≈1 交易日，約 40 日", MarketMode::US, "USD", 10000.0,
        {
            {"GME", 0.06, {{0, 17}, {3, 19}, {5, 31}, {6, 39}, {8, 36}, {9, 43}, {10, 39}, {11, 43}, {12, 65},
                           {13, 76, 159, 61}, {14, 148}, {15, 347, 380, 249}, {16, 193, 483, 112},
                           {17, 325, 413, 250}, {18, 225}, {19, 90}, {20, 92}, {21, 54}, {22, 63}, {23, 60},
                           {24, 50}, {25, 52}, {26, 49}, {27, 52}, {28, 45}, {29, 40}, {30, 46}, {31, 45},
                           {32, 92}, {33, 108, 184, 101}, {34, 101}, {35, 120}, {36, 118}, {37, 124}, {38, 132},
                           {39, 194, 348, 172}}},
            {"AMC", 0.06, {{0, 2.0}, {5, 2.2}, {9, 3.0}, {12, 3.5}, {13, 4.0}, {14, 4.96}, {15, 19.9, 20.4},
                           {16, 8.6, 25.4}, {17, 13.3}, {18, 13.3}, {19, 7.8}, {21, 6.8}, {24, 6.0}, {29, 5.8},
                           {32, 9.1}, {33, 9.0}, {35, 8.1}, {37, 9.7}, {39, 11.2}}},
            {"BB", 0.05, {{0, 6.6}, {5, 8.0}, {7, 12.0}, {9, 14.0}, {12, 14.1}, {13, 17.6}, {14, 16.5},
                          {15, 25.1, 28.8}, {16, 14.1}, {17, 14.1}, {19, 12.0}, {21, 11.2}, {24, 12.5},
                          {29, 10.5}, {32, 10.6}, {35, 10.0}, {39, 10.9}}},
        });
}

inline ScenarioPack packDotcomUs() {
    return buildPack(
        "DOTCOM_US", "網路泡沫崩盤（2000-02）",
        "NASDAQ 高檔反轉後，科技股估值快速收縮；QQQ、Cisco(CSCO)與 Microsoft(MSFT)\n"
        "走勢分化。用來練習趨勢反轉與避免只因低價而抄底。關鍵點為近似值。",
        "每根≈2~3 交易日，壓縮約 4 個月", MarketMode::US, "USD", 10000.0,
        {
            {"QQQ", 0.025, {{0, 120}, {5, 112}, {10, 100}, {14, 90}, {18, 82}, {22, 72},
                            {26, 65}, {30, 55}, {34, 48}, {38, 42}, {42, 38}, {47, 34}}},
            {"CSCO", 0.04, {{0, 80}, {5, 72}, {10, 60}, {14, 52}, {18, 44}, {22, 36},
                            {26, 30}, {30, 24}, {34, 20}, {38, 17}, {42, 14}, {47, 12}}},
            {"MSFT", 0.022, {{0, 58}, {5, 56}, {10, 53}, {14, 49}, {18, 45}, {22, 42},
                             {26, 39}, {30, 36}, {34, 34}, {38, 32}, {42, 30}, {47, 28}}},
        });
}

inline ScenarioPack packRateHikesUs2022() {
    return buildPack(
        "RATE_HIKES_US_2022", "升息與通膨熊市（2022）",
        "通膨升溫與快速升息壓抑股票、長天期債券；年中反彈後再探底。\n"
        "SPY、QQQ、TLT 為近似事件路徑，練習辨認反彈中的下跌趨勢。",
        "每根≈1 週，壓縮約 1 年", MarketMode::US, "USD", 10000.0,
        {
            {"SPY", 0.018, {{0, 477}, {5, 450}, {10, 430}, {15, 410}, {20, 390}, {25, 365},
                            {30, 390}, {35, 370}, {40, 350}, {45, 358}, {49, 384}}},
            {"QQQ", 0.025, {{0, 397}, {5, 365}, {10, 340}, {15, 310}, {20, 285}, {25, 270},
                            {30, 300}, {35, 275}, {40, 260}, {45, 265}, {49, 285}}},
            {"TLT", 0.018, {{0, 148}, {5, 140}, {10, 132}, {15, 125}, {20, 118}, {25, 110},
                            {30, 114}, {35, 108}, {40, 102}, {45, 100}, {49, 108}}},
        });
}

inline ScenarioPack packTaiwan2022() {
    return buildPack(
        "TW_BEAR_2022", "台股升息修正（2022）",
        "加權市場與大型電子股在全球升息環境下回檔，年中反彈未能立即扭轉趨勢。\n"
        "2330、0050、鴻海(2317) 為教學近似路徑，並非還原股價。",
        "每根≈1 週，壓縮約 1 年", MarketMode::TW, "TWD", 300000.0,
        {
            {"2330", 0.022, {{0, 615}, {5, 590}, {10, 560}, {15, 525}, {20, 495}, {25, 445},
                             {30, 485}, {35, 455}, {40, 430}, {45, 445}, {49, 455}}},
            {"0050", 0.02, {{0, 145}, {5, 137}, {10, 128}, {15, 120}, {20, 112}, {25, 101},
                            {30, 110}, {35, 103}, {40, 96}, {45, 100}, {49, 102}}},
            {"2317", 0.025, {{0, 104}, {5, 99}, {10, 93}, {15, 88}, {20, 81}, {25, 75},
                            {30, 83}, {35, 78}, {40, 72}, {45, 75}, {49, 77}}},
        });
}

inline ScenarioPack packAsianCrisisHk1997() {
    return buildPack(
        "ASIAN_CRISIS_HK_1997", "亞洲金融風暴（港股，1997）",
        "港股在區域金融震盪中急跌，期間反彈後再創低點；以恒生指數與大型股\n"
        "路徑呈現高波動風險。關鍵價位為教學近似值。",
        "每根≈數個交易日，壓縮約 5 個月", MarketMode::HK, "HKD", 80000.0,
        {
            {"HSI", 0.03, {{0, 16000}, {5, 15000}, {10, 13800}, {15, 12500}, {19, 10000},
                           {22, 11500}, {26, 10500}, {30, 9000}, {34, 8200}, {39, 10000}}},
            {"0005", 0.025, {{0, 110}, {5, 103}, {10, 95}, {15, 88}, {19, 74},
                             {22, 82}, {26, 78}, {30, 68}, {34, 62}, {39, 70}}},
            {"0700", 0.03, {{0, 18}, {5, 17}, {10, 15}, {15, 14}, {19, 11},
                            {22, 13}, {26, 12}, {30, 10}, {34, 9}, {39, 12}}},
        });
}

// ---------------------------------------------------------------- runtime loader

inline bool parseMarket(const std::string& s, MarketMode& out) {
    if (s == "US") out = MarketMode::US;
    else if (s == "TW") out = MarketMode::TW;
    else if (s == "HK") out = MarketMode::HK;
    else if (s == "CRYPTO" || s == "Crypto") out = MarketMode::Crypto;
    else if (s == "FUNDS" || s == "Funds") out = MarketMode::Funds;
    else if (s == "FUTURES" || s == "Futures") out = MarketMode::Futures;
    else return false;
    return true;
}

inline std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::string unescapeNl(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') {
            out += '\n';
            ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------- CSV OHLC loader
// CSV columns (header optional, case-insensitive):
//   open,high,low,close
//   day,open,high,low,close   (row order = bar index)
//   date,open,high,low,close  (date retained for chart labels)
// Separators: comma or whitespace. Lines starting with # skipped.
// Paths in .pack `ohlc=SYMBOL|file.csv` are relative to the .pack file directory.

inline bool parseCsvLine(const std::string& line, std::vector<std::string>& cols) {
    cols.clear();
    std::string cur;
    for (char ch : line) {
        if (ch == ',' || ch == '\t' || ch == ';') {
            cols.push_back(trimStr(cur));
            cur.clear();
        } else {
            cur += ch;
        }
    }
    cols.push_back(trimStr(cur));
    while (!cols.empty() && cols.back().empty()) cols.pop_back();
    return !cols.empty();
}

inline bool isCsvHeaderToken(const std::string& s) {
    std::string t;
    for (char c : s) t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return t == "open" || t == "high" || t == "low" || t == "close" || t == "day" ||
           t == "date" || t == "time" || t == "o" || t == "h" || t == "l" || t == "c";
}

inline bool loadOhlcCsv(const std::filesystem::path& path, std::vector<OHLC>& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "無法開啟 CSV: " + path.filename().string(); return false; }
    out.clear();
    std::string line;
    bool checkedHeader = false;
    int oIdx = 0, hIdx = 1, lIdx = 2, cIdx = 3, timeIdx = -1;
    while (std::getline(in, line)) {
        line = trimStr(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        if (!parseCsvLine(line, cols)) continue;
        if (!checkedHeader) {
            checkedHeader = true;
            bool headerish = false;
            for (const auto& c : cols) if (isCsvHeaderToken(c)) headerish = true;
            if (headerish) {
                oIdx = hIdx = lIdx = cIdx = -1;
                for (size_t i = 0; i < cols.size(); ++i) {
                    std::string t;
                    for (char ch : cols[i]) t += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                    if (t == "open" || t == "o") oIdx = static_cast<int>(i);
                    else if (t == "high" || t == "h") hIdx = static_cast<int>(i);
                    else if (t == "low" || t == "l") lIdx = static_cast<int>(i);
                    else if (t == "close" || t == "c") cIdx = static_cast<int>(i);
                    else if (t == "date" || t == "time" || t == "datetime" ||
                             t == "timestamp") timeIdx = static_cast<int>(i);
                }
                if (oIdx < 0 || hIdx < 0 || lIdx < 0 || cIdx < 0) {
                    err = "CSV 表頭需含 open,high,low,close";
                    return false;
                }
                continue;
            }
            // no header: if 5+ cols assume day/date first
            if (cols.size() >= 5) {
                oIdx = 1; hIdx = 2; lIdx = 3; cIdx = 4;
                timeIdx = 0;
            }
            else { oIdx = 0; hIdx = 1; lIdx = 2; cIdx = 3; }
        }
        int need = std::max(std::max(oIdx, hIdx), std::max(lIdx, cIdx));
        if (static_cast<int>(cols.size()) <= need) {
            err = "CSV 欄位不足: " + line;
            return false;
        }
        OHLC b;
        b.open = std::atof(cols[oIdx].c_str());
        b.high = std::atof(cols[hIdx].c_str());
        b.low = std::atof(cols[lIdx].c_str());
        b.close = std::atof(cols[cIdx].c_str());
        if (timeIdx >= 0 && timeIdx < static_cast<int>(cols.size()))
            b.timestamp = cols[timeIdx];
        if (b.open <= 0 || b.high <= 0 || b.low <= 0 || b.close <= 0) {
            err = "CSV 價格必須 > 0: " + line;
            return false;
        }
        if (b.high < b.low) std::swap(b.high, b.low);
        b.high = std::max(b.high, std::max(b.open, b.close));
        b.low = std::min(b.low, std::min(b.open, b.close));
        b.low = std::max(PACK_PRICE_FLOOR, b.low);
        out.push_back(b);
        if (out.size() > 2000) {
            err = "CSV 超過 2000 根";
            return false;
        }
    }
    if (out.size() < 2) {
        err = "CSV 至少需要 2 根 K 棒: " + path.filename().string();
        return false;
    }
    return true;
}

inline ScenarioPack buildPackFromBars(const std::string& id, const std::string& title, const std::string& blurb,
                                      const std::string& barNote, MarketMode m, const std::string& ccy, double cash,
                                      const std::vector<std::string>& symbols,
                                      const std::vector<std::vector<OHLC>>& bars) {
    ScenarioPack p;
    p.id = id;
    p.title = title;
    p.blurb = blurb;
    p.barNote = barNote;
    p.market = m;
    p.currency = ccy;
    p.startCash = cash;
    p.symbols = symbols;
    p.bars = bars;
    // Align lengths to max
    size_t n = 0;
    for (const auto& b : p.bars) n = std::max(n, b.size());
    for (auto& b : p.bars) {
        if (b.empty()) continue;
        while (b.size() < n) b.push_back(b.back());
    }
    return p;
}

// Format: key=value lines.
//   symbol=NAME|noise|day:close[:high[:low]],...   (keypoint stub → generated OHLC)
//   ohlc=NAME|file.csv                             (inject historical OHLC CSV; path relative to .pack dir)
// Exactly 3 series total from symbol= and/or ohlc= (mixed OK). Returns false on malformed files.
inline bool loadPackFile(const std::filesystem::path& path, ScenarioPack& out, std::string& err) {
    std::ifstream in(path);
    if (!in) { err = "無法開啟"; return false; }
    std::string id, title, blurb, barNote = "示意", ccy;
    MarketMode m = MarketMode::US;
    bool haveMarket = false;
    double cash = 0;
    std::vector<KeySeries> series;
    std::vector<std::string> csvSymbols;
    std::vector<std::vector<OHLC>> csvBars;
    std::string line;
    while (std::getline(in, line)) {
        line = trimStr(line);
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimStr(line.substr(0, eq));
        std::string v = trimStr(line.substr(eq + 1));
        if (k == "id") id = v;
        else if (k == "title") title = v;
        else if (k == "blurb") blurb = unescapeNl(v);
        else if (k == "bars") barNote = v;
        else if (k == "market") haveMarket = parseMarket(v, m);
        else if (k == "currency") ccy = v;
        else if (k == "cash") cash = std::atof(v.c_str());
        else if (k == "ohlc") {
            size_t bar = v.find('|');
            if (bar == std::string::npos) { err = "ohlc 格式: NAME|file.csv"; return false; }
            std::string sym = trimStr(v.substr(0, bar));
            std::string rel = trimStr(v.substr(bar + 1));
            if (sym.empty() || rel.empty()) { err = "ohlc 缺少名稱或檔名"; return false; }
            std::filesystem::path csvPath = path.parent_path() / rel;
            std::vector<OHLC> bars;
            std::string cerr;
            if (!loadOhlcCsv(csvPath, bars, cerr)) { err = cerr; return false; }
            if (csvSymbols.size() + series.size() >= 3 && csvSymbols.size() + series.size() != 2) {
                // allow collecting up to 3; check at end
            }
            if (csvSymbols.size() + series.size() >= 3) {
                err = "超過 3 檔標的";
                return false;
            }
            csvSymbols.push_back(sym);
            csvBars.push_back(std::move(bars));
        }
        else if (k == "symbol") {
            KeySeries ks;
            std::stringstream ss(v);
            std::string name, noise, keys;
            std::getline(ss, name, '|');
            std::getline(ss, noise, '|');
            std::getline(ss, keys);
            ks.symbol = trimStr(name);
            ks.noise = std::clamp(std::atof(noise.c_str()), 0.0, 0.3);
            std::stringstream ksx(keys);
            std::string tok;
            while (std::getline(ksx, tok, ',')) {
                tok = trimStr(tok);
                if (tok.empty()) continue;
                KeyPt kp;
                double vals[4] = {0, 0, 0, 0};
                int nv = 0;
                std::stringstream ts(tok);
                std::string part;
                while (nv < 4 && std::getline(ts, part, ':')) vals[nv++] = std::atof(part.c_str());
                if (nv < 2 || vals[0] < 0 || vals[0] > 400 || vals[1] <= 0) {
                    err = "關鍵點格式錯誤: " + tok;
                    return false;
                }
                kp.day = static_cast<int>(vals[0]);
                kp.close = vals[1];
                kp.high = nv > 2 ? vals[2] : 0;
                kp.low = nv > 3 ? vals[3] : 0;
                ks.keys.push_back(kp);
            }
            if (ks.symbol.empty() || ks.keys.size() < 2) {
                err = "symbol 行至少需 2 個關鍵點";
                return false;
            }
            if (csvSymbols.size() + series.size() >= 3) {
                err = "超過 3 檔標的";
                return false;
            }
            series.push_back(ks);
        }
    }
    size_t nSeries = series.size() + csvSymbols.size();
    if (id.empty() || title.empty() || !haveMarket || ccy.empty() || cash <= 0 || nSeries != 3) {
        err = "缺少 id/title/market/currency/cash 或標的不足／超過 3 檔（可用 symbol= 或 ohlc=）";
        return false;
    }
    if (csvSymbols.empty()) {
        out = buildPack(id, title, blurb, barNote, m, ccy, cash, series);
    } else if (series.empty()) {
        out = buildPackFromBars(id, title, blurb, barNote, m, ccy, cash, csvSymbols, csvBars);
    } else {
        // Mixed: build keypoints then append CSV series (order = file order interleaved by parse order)
        // Rebuild in file order: we only have separate vectors; concatenate keypoint packs then csv.
        ScenarioPack keyPack = buildPack(id, title, blurb, barNote, m, ccy, cash, series);
        std::vector<std::string> syms = keyPack.symbols;
        std::vector<std::vector<OHLC>> bars = keyPack.bars;
        for (size_t i = 0; i < csvSymbols.size(); ++i) {
            syms.push_back(csvSymbols[i]);
            bars.push_back(csvBars[i]);
        }
        out = buildPackFromBars(id, title, blurb, barNote, m, ccy, cash, syms, bars);
    }
    out.source = path.filename().string();
    return true;
}

inline std::vector<std::filesystem::path> scenarioDirs() {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
#ifdef _WIN32
    wchar_t wbuf[4096];
    unsigned long wn = GetModuleFileNameW(nullptr, wbuf, 4096);
    if (wn > 0 && wn < 4096) {
        wbuf[wn] = 0;
        dirs.push_back(fs::path(wbuf).parent_path() / "scenarios");
    }
#elif defined(__APPLE__)
    uint32_t pathSize = 0;
    _NSGetExecutablePath(nullptr, &pathSize);
    if (pathSize > 0) {
        std::vector<char> pathBuffer(pathSize);
        if (_NSGetExecutablePath(pathBuffer.data(), &pathSize) == 0) {
            const fs::path executableDir = fs::path(pathBuffer.data()).parent_path();
            dirs.push_back(executableDir / ".." / "Resources" / "scenarios");
            dirs.push_back(executableDir / "scenarios");
        }
    }
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        dirs.push_back(fs::path(buf).parent_path() / "scenarios");
    }
#endif
    std::error_code ec;
    fs::path cwd = fs::current_path(ec) / "scenarios";
    if (!ec) dirs.push_back(cwd);
    return dirs;
}

struct PackRegistry {
    std::vector<ScenarioPack> packs;
    std::vector<std::string> loadErrors;
};

inline const PackRegistry& packRegistry() {
    static PackRegistry reg = [] {
        PackRegistry r;
        r.packs = {packCovidUs(), pack2008Us(), packBlackMonday(), packMeme2021(),
                   packDotcomUs(), packRateHikesUs2022(), packCovidTw(), packTaiwan2022(),
                   packCovidHk(), packAsianCrisisHk1997(), packCryptoWinter()};
        namespace fs = std::filesystem;
        std::vector<fs::path> seen;
        for (const auto& dir : scenarioDirs()) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            fs::path canon = fs::weakly_canonical(dir, ec);
            if (!ec && std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
            if (!ec) seen.push_back(canon);
            std::vector<fs::path> files;
            for (const auto& ent : fs::directory_iterator(dir, ec))
                if (ent.path().extension() == ".pack") files.push_back(ent.path());
            std::sort(files.begin(), files.end());
            for (const auto& f : files) {
                ScenarioPack p;
                std::string err;
                if (!loadPackFile(f, p, err)) {
                    r.loadErrors.push_back(f.filename().string() + ": " + err);
                    continue;
                }
                bool dup = false;
                for (const auto& q : r.packs) dup = dup || (q.id == p.id);
                if (dup) {
                    r.loadErrors.push_back(f.filename().string() + ": id 重複，略過");
                    continue;
                }
                r.packs.push_back(std::move(p));
            }
        }
        return r;
    }();
    return reg;
}

inline const std::vector<ScenarioPack>& allPacks() { return packRegistry().packs; }

inline bool packIsImported(const ScenarioPack& p) {
    return p.source != "內建";
}

inline int importedPackCount() {
    int n = 0;
    for (const auto& p : allPacks()) if (packIsImported(p)) ++n;
    return n;
}
