#pragma once

// Open Bell — deterministic market-scenario generation (header-only).
// Extracted from the Open Bell project (https://github.com/Sofaterdion-gif/openbell-core).

#include "market.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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

struct KeyPt {
    int day = 0;
    double close = 0;
    double high = 0;
    double low = 0;
};

struct KeySeries {
    std::string symbol;
    double noise = 0.0;
    std::vector<KeyPt> keys;
};

struct ScenarioPack {
    std::string id;
    std::string title;
    std::string blurb;
    std::string barNote;
    std::string source;
    MarketMode market = MarketMode::US;
    std::string currency;
    double startCash = 0;
    std::vector<std::string> symbols;
    std::vector<std::vector<OHLC>> bars;
};

inline std::string marketCode(MarketMode m) {
    switch (m) {
        case MarketMode::US: return "US";
        case MarketMode::TW: return "TW";
        case MarketMode::HK: return "HK";
        case MarketMode::Crypto: return "CRYPTO";
        case MarketMode::Funds: return "FUNDS";
        case MarketMode::Futures: return "FUTURES";
    }
    return "US";
}

inline bool parseMarket(const std::string& s, MarketMode& out) {
    std::string t;
    for (char c : s) t += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (t == "US") out = MarketMode::US;
    else if (t == "TW") out = MarketMode::TW;
    else if (t == "HK") out = MarketMode::HK;
    else if (t == "CRYPTO" || t == "CRYPTO") out = MarketMode::Crypto;
    else if (t == "FUNDS") out = MarketMode::Funds;
    else if (t == "FUTURES") out = MarketMode::Futures;
    else return false;
    return true;
}

// FNV-1a 32-bit — stable across platforms/processes, unlike std::hash.
inline std::uint32_t packHash(const std::string& s) {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

// Deterministic seeded PRNG → same pack id always produces the same path.
inline std::mt19937 packRng(const std::string& seedKey) {
    return std::mt19937(packHash(seedKey));
}

inline std::string trimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Unescape \\n → newline (blurb field).
inline std::string unescapeNl(const std::string& s) {
    std::string out;
    out.reserve(s.size());
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

// Narrative keypoints → deterministic OHLC path.
// Log-linear interpolation between keypoint closes; AR(1) noise (mean-reverting,
// pinned to zero at every keypoint) fills the in-between bars; optional absolute
// high/low overrides at keypoints. Same seed ⇒ same bars, on every platform.
inline std::vector<OHLC> keysToBars(const std::string& seedKey,
                                    const std::vector<KeyPt>& keys,
                                    double noiseAmount,
                                    int totalBars) {
    std::vector<OHLC> out;
    if (keys.empty() || totalBars <= 0) return out;
    out.reserve(static_cast<size_t>(totalBars));

    std::mt19937 rng = packRng(seedKey);
    std::normal_distribution<double> nd(0.0, 1.0);

    double carry = 0.0;
    auto interpClose = [&](int day) {
        if (day <= keys.front().day) return keys.front().close;
        if (day >= keys.back().day) return keys.back().close;
        for (size_t k = 0; k + 1 < keys.size(); ++k) {
            if (day >= keys[k].day && day <= keys[k + 1].day) {
                double span = static_cast<double>(keys[k + 1].day - keys[k].day);
                if (span <= 0.0) return keys[k].close;
                double t = (day - keys[k].day) / span;
                double a = std::log(keys[k].close);
                double b = std::log(keys[k + 1].close);
                return std::exp(a + (b - a) * t);
            }
        }
        return keys.back().close;
    };

    for (int i = 0; i < totalBars; ++i) {
        const int day = i + 1;
        double c = interpClose(day);
        // Noise pinned to zero at keypoints so the narrative is exact.
        bool pinned = false;
        for (const auto& kp : keys)
            if (kp.day == day) { pinned = true; break; }
        if (!pinned && noiseAmount > 0.0) {
            double shock = nd(rng);
            carry = 0.65 * carry + 0.35 * shock;
            c *= std::exp(carry * noiseAmount);
        }
        c = std::max(PACK_PRICE_FLOOR, c);
        const double o = (i == 0) ? c : out.back().close;
        double hi = std::max(o, c) * 1.01;
        double lo = std::min(o, c) * 0.99;
        for (const auto& kp : keys) {
            if (kp.day == day && kp.high > 0) hi = std::max(hi, kp.high);
            if (kp.day == day && kp.low > 0) lo = std::min(lo, kp.low);
        }
        lo = std::min(lo, std::min(o, c));
        hi = std::max(hi, std::max(o, c));
        out.push_back({o, hi, lo, c, ""});
    }
    return out;
}

// One full pack from 3 keypoint series.
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
    for (const auto& ks : series) {
        p.symbols.push_back(ks.symbol);
        p.bars.push_back(keysToBars(id + "/" + ks.symbol, ks.keys, ks.noise, 240));
    }
    return p;
}

// ---------- Built-in educational scenario packs ----------

inline ScenarioPack packBlackMondayUS() {
    return buildPack(
        "BLACK_MONDAY_US", "1987 黑色星期一", "1987/10/19，道瓊單日 -22.6%。恐慌性拋售與投資組合保險連鎖反應。", "歷史事件重現",
        MarketMode::US, "USD", 100000,
        {{"DJIA", 0.015, {{1, 282.0}, {15, 225.0}, {40, 175.0}, {80, 200.0}}},
         {"S&P500", 0.018, {{1, 325.0}, {15, 260.0}, {40, 210.0}, {80, 240.0}}},
         {"VIX", 0.03, {{1, 20.0}, {15, 150.0}, {40, 80.0}, {80, 40.0}}}});
}

inline ScenarioPack packCovidUS() {
    return buildPack(
        "COVID_US", "2020 COVID 崩盤", "2020/2-3 月，新冠爆發，S&P500 腰斬後 V 轉。聯準會無限 QE。", "歷史事件重現",
        MarketMode::US, "USD", 100000,
        {{"SPX", 0.025, {{1, 3380.0}, {30, 2237.0}, {60, 2900.0}, {120, 3500.0}}},
         {"OIL", 0.04, {{1, 52.0}, {40, 25.0}, {60, 30.0}, {120, 40.0}}},
         {"GOLD", 0.015, {{1, 1650.0}, {30, 1500.0}, {60, 1750.0}, {120, 2050.0}}}});
}

inline ScenarioPack packCovidTW() {
    return buildPack(
        "COVID_TW", "2020 COVID 台股", "2020/3 台股閃崩後強彈，外資回補。", "歷史事件重現",
        MarketMode::TW, "TWD", 1000000,
        {{"TWII", 0.022, {{1, 11800.0}, {25, 8600.0}, {60, 11500.0}, {120, 13000.0}}},
         {"TSMC", 0.028, {{1, 320.0}, {25, 235.0}, {60, 310.0}, {120, 450.0}}},
         {"USDTWD", 0.008, {{1, 30.2}, {25, 30.5}, {60, 29.5}, {120, 28.5}}}});
}

inline ScenarioPack packCovidHK() {
    return buildPack(
        "COVID_HK", "2020 COVID 港股", "2020 疫情疊加社會事件，恆指破淨後反彈。", "歷史事件重現",
        MarketMode::HK, "HKD", 800000,
        {{"HSI", 0.025, {{1, 28000.0}, {40, 21000.0}, {80, 26000.0}, {120, 29000.0}}},
         {"HKEX", 0.02, {{1, 380.0}, {40, 270.0}, {80, 340.0}, {120, 390.0}}},
         {"USDCNH", 0.006, {{1, 6.95}, {40, 7.12}, {80, 7.05}, {120, 6.85}}}});
}

inline ScenarioPack pack2008US() {
    return buildPack(
        "2008_US", "2008 金融海嘯", "雷曼倒閉，房貸泡沫破裂。S&P500 自高點 -57%。", "歷史事件重現",
        MarketMode::US, "USD", 100000,
        {{"SPX", 0.02, {{1, 1560.0}, {80, 670.0}, {160, 900.0}, {240, 1150.0}}},
         {"LEH", 0.05, {{1, 65.0}, {80, 0.2}, {160, 0.0}, {240, 0.0}}},
         {"GOLD", 0.015, {{1, 900.0}, {80, 1000.0}, {160, 1150.0}, {240, 1250.0}}}});
}

inline ScenarioPack packMeme2021US() {
    return buildPack(
        "MEME_2021_US", "2021 Meme 股狂潮", "GameStop 軋空，散戶大戰華爾街。AMC/GME 百倍波動。", "歷史事件重現",
        MarketMode::US, "USD", 50000,
        {{"GME", 0.06, {{1, 18.0}, {40, 325.0}, {60, 40.0}, {120, 180.0}}},
         {"AMC", 0.055, {{1, 2.5}, {40, 20.0}, {60, 5.0}, {120, 35.0}}},
         {"SPX", 0.015, {{1, 3800.0}, {40, 3850.0}, {60, 3900.0}, {120, 4700.0}}}});
}

inline ScenarioPack packDotcomUS() {
    return buildPack(
        "DOTCOM_US", "2000 網路泡沫", "NASDAQ 從 5048 崩到 1114。無獲利新創集體歸零。", "歷史事件重現",
        MarketMode::US, "USD", 100000,
        {{"IXIC", 0.022, {{1, 4700.0}, {100, 1900.0}, {180, 1400.0}, {240, 2200.0}}},
         {"CSCO", 0.03, {{1, 80.0}, {100, 25.0}, {180, 15.0}, {240, 22.0}}},
         {"YHOO", 0.04, {{1, 250.0}, {100, 40.0}, {180, 12.0}, {240, 28.0}}}});
}

inline ScenarioPack packRateHikes2022US() {
    return buildPack(
        "RATE_HIKES_US_2022", "2022 暴力升息", "聯準會一年升息 17 碼，股債雙殺，美元強勢。", "歷史事件重現",
        MarketMode::US, "USD", 100000,
        {{"SPX", 0.02, {{1, 4800.0}, {90, 3600.0}, {160, 4100.0}, {240, 4500.0}}},
         {"TLT", 0.015, {{1, 150.0}, {90, 95.0}, {160, 105.0}, {240, 115.0}}},
         {"DXY", 0.01, {{1, 96.0}, {90, 113.0}, {160, 108.0}, {240, 102.0}}}});
}

inline ScenarioPack packTWBear2022() {
    return buildPack(
        "TW_BEAR_2022", "2022 台股空頭", "升息+地緣風險，台股自 18600 跌至 12600。", "歷史事件重現",
        MarketMode::TW, "TWD", 1000000,
        {{"TWII", 0.02, {{1, 18200.0}, {90, 13800.0}, {160, 14500.0}, {240, 15500.0}}},
         {"TSMC", 0.025, {{1, 630.0}, {90, 380.0}, {160, 430.0}, {240, 520.0}}},
         {"USDTWD", 0.008, {{1, 27.8}, {90, 31.5}, {160, 30.8}, {240, 30.0}}}});
}

inline ScenarioPack packAsianCrisisHK1997() {
    return buildPack(
        "ASIAN_CRISIS_HK_1997", "1997 亞洲金融風暴", "索羅斯狙擊港匯，恆指劇震，港府入市護盤。", "歷史事件重現",
        MarketMode::HK, "HKD", 800000,
        {{"HSI", 0.03, {{1, 16000.0}, {60, 9000.0}, {120, 14000.0}, {240, 16000.0}}},
         {"USDKRW", 0.02, {{1, 890.0}, {60, 1700.0}, {120, 1300.0}, {240, 1100.0}}},
         {"GOLD", 0.012, {{1, 290.0}, {60, 310.0}, {120, 300.0}, {240, 290.0}}}});
}

inline ScenarioPack packCryptoWinter2022() {
    return buildPack(
        "CRYPTO_WINTER_2022", "2022 幣圈寒冬", "LUNA/FTX 連環爆，BTC 自 69K 跌至 15K。", "歷史事件重現",
        MarketMode::Crypto, "USD", 50000,
        {{"BTC", 0.04, {{1, 48000.0}, {80, 17000.0}, {160, 21000.0}, {240, 16500.0}}},
         {"LUNA", 0.08, {{1, 90.0}, {60, 0.0001}, {120, 0.0001}, {240, 0.0001}}},
         {"ETH", 0.045, {{1, 3800.0}, {80, 900.0}, {160, 1600.0}, {240, 1200.0}}}});
}

inline const std::vector<ScenarioPack>& allPacks() {
    static const std::vector<ScenarioPack> packs = [] {
        std::vector<ScenarioPack> out;
        auto add = [&out](const ScenarioPack& p) {
            if (std::find_if(out.begin(), out.end(), [&](const ScenarioPack& q) { return q.id == p.id; }) == out.end())
                out.push_back(p);
        };
        add(packBlackMondayUS());
        add(packCovidUS());
        add(packCovidTW());
        add(packCovidHK());
        add(pack2008US());
        add(packMeme2021US());
        add(packDotcomUS());
        add(packRateHikes2022US());
        add(packTWBear2022());
        add(packAsianCrisisHK1997());
        add(packCryptoWinter2022());
        return out;
    }();
    return packs;
}

// ---------- .pack file loading ----------

inline std::filesystem::path exeDir() {
    std::error_code ec;
#ifdef _WIN32
    wchar_t wbuf[4096];
    unsigned long wn = GetModuleFileNameW(nullptr, wbuf, 4096);
    if (wn == 0) return std::filesystem::current_path(ec);
    return std::filesystem::path(std::wstring(wbuf, wn)).parent_path();
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return std::filesystem::current_path(ec);
    return std::filesystem::path(buf).parent_path();
#else
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::filesystem::current_path(ec);
    buf[n] = '\0';
    return std::filesystem::path(buf).parent_path();
#endif
}

// Discover .pack files in exeDir() and exeDir()/packs. Built-in packs always come first.
inline std::vector<ScenarioPack> packRegistry() {
    std::vector<ScenarioPack> out = allPacks();
    std::vector<std::filesystem::path> seen;
    auto scanDir = [&](const std::filesystem::path& dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;
        std::filesystem::path canon = std::filesystem::weakly_canonical(dir, ec);
        if (!ec && std::find(seen.begin(), seen.end(), canon) != seen.end()) return;
        if (!ec) seen.push_back(canon);
        std::vector<std::pair<std::uint64_t, size_t>> order;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            if (e.path().extension() != ".pack") continue;
            ScenarioPack p;
            std::string err;
            if (!loadPackFile(e.path(), p, err)) continue;
            if (std::find_if(out.begin(), out.end(), [&](const ScenarioPack& q) { return q.id == p.id; }) != out.end()) continue;
            out.push_back(std::move(p));
        }
        (void)order;
    };
    scanDir(exeDir());
    scanDir(exeDir() / "packs");
    return out;
}

inline bool parseCsvLine(const std::string& line, std::vector<std::string>& cols) {
    std::string cur;
    cols.clear();
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
