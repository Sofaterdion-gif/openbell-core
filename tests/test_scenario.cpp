#include "check.hpp"

#include <openbell/scenario.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace {

bool closeRel(double a, double b, double relTol = 1e-9) {
    return std::abs(a - b) <= relTol * std::max(std::abs(a), std::abs(b));
}

void testDeterminism() {
    const std::vector<KeyPt> keys{{0, 100}, {5, 120}, {12, 90}, {20, 110}};
    const auto a = keysToBars("seed/A", keys, 0.02, 21);
    const auto b = keysToBars("seed/A", keys, 0.02, 21);
    OB_CHECK(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        OB_CHECK(a[i].open == b[i].open);
        OB_CHECK(a[i].high == b[i].high);
        OB_CHECK(a[i].low == b[i].low);
        OB_CHECK(a[i].close == b[i].close);
    }
    // Different seed → different path (with this much noise, identical output is implausible).
    const auto c = keysToBars("seed/B", keys, 0.02, 21);
    bool anyDiff = false;
    for (size_t i = 0; i < a.size() && i < c.size(); ++i)
        anyDiff = anyDiff || (a[i].close != c[i].close);
    OB_CHECK(anyDiff);
}

void testKeypointPinning() {
    // Noise is pinned to zero at every keypoint → closes hit the narrative exactly.
    const std::vector<KeyPt> keys{{0, 100.0}, {10, 50.0}, {15, 200.0, 250.0, 180.0}};
    const auto bars = keysToBars("pin/test", keys, 0.03, 16);
    OB_CHECK(bars.size() == 16);
    OB_CHECK(closeRel(bars[0].close, 100.0));
    OB_CHECK(closeRel(bars[10].close, 50.0));
    OB_CHECK(closeRel(bars[15].close, 200.0));
    OB_CHECK(bars[15].high >= 250.0);   // absolute high override
    OB_CHECK(bars[15].low <= 180.0);    // absolute low override
}

void testOhlcValidityAllPacks() {
    for (const auto& p : allPacks()) {
        OB_CHECK(p.symbols.size() == 3);
        OB_CHECK(p.bars.size() == 3);
        size_t n = p.bars[0].size();
        OB_CHECK(n >= 2);
        for (const auto& series : p.bars) {
            OB_CHECK(series.size() == n);  // aligned lengths
            for (const auto& b : series) {
                OB_CHECK(b.open > 0 && b.high > 0 && b.low > 0 && b.close > 0);
                OB_CHECK(b.high >= b.open - 1e-9);
                OB_CHECK(b.high >= b.close - 1e-9);
                OB_CHECK(b.low <= b.open + 1e-9);
                OB_CHECK(b.low <= b.close + 1e-9);
                OB_CHECK(b.low >= PACK_PRICE_FLOOR);
            }
        }
    }
}

void testBuiltinRegistry() {
    const auto& packs = allPacks();
    OB_CHECK(packs.size() >= 11);
    std::set<std::string> ids;
    for (const auto& p : packs) ids.insert(p.id);
    OB_CHECK(ids.size() == packs.size());  // unique ids
    OB_CHECK(ids.count("BLACK_MONDAY_US") == 1);
    OB_CHECK(ids.count("COVID_US") == 1);
    OB_CHECK(ids.count("CRYPTO_WINTER_2022") == 1);
    OB_CHECK(packs.front().source == "內建");
}

void testPackFileLoaderRoundTrip() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openbell_core_test";
    fs::create_directories(dir);
    const fs::path packPath = dir / "test.pack";
    {
        std::ofstream out(packPath);
        out << "id=TEST_PACK\n"
            << "title=Round Trip\n"
            << "market=US\n"
            << "currency=USD\n"
            << "cash=10000\n"
            << "symbol=AAA|0.01|0:100,4:120\n"
            << "symbol=BBB|0.02|0:50,4:40\n"
            << "symbol=CCC|0.015|0:200,4:180\n";
    }
    ScenarioPack p;
    std::string err;
    OB_CHECK(loadPackFile(packPath, p, err));
    OB_CHECK(err.empty());
    OB_CHECK(p.id == "TEST_PACK");
    OB_CHECK(p.market == MarketMode::US);
    OB_CHECK(p.symbols.size() == 3);
    OB_CHECK(p.bars[0].size() == 5);
    OB_CHECK(closeRel(p.bars[0][4].close, 120.0));  // keypoint pinned after file load
    OB_CHECK(p.source == "test.pack");

    fs::remove(packPath);
}

void testCsvLoader() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openbell_core_test";
    fs::create_directories(dir);
    const fs::path csvPath = dir / "sample.csv";
    {
        std::ofstream out(csvPath);
        out << "date,open,high,low,close\n"
            << "2024-01-01,100,105,95,102\n"
            << "2024-01-02,102,103,98,99\n"
            << "# comment line skipped\n"
            << "2024-01-03,99,101,97,100\n";
    }
    std::vector<OHLC> bars;
    std::string err;
    OB_CHECK(loadOhlcCsv(csvPath, bars, err));
    OB_CHECK(bars.size() == 3);
    OB_CHECK(bars[0].timestamp == "2024-01-01");
    OB_CHECK(closeRel(bars[0].high, 105.0));
    OB_CHECK(closeRel(bars[0].low, 95.0));

    // high < low in source → loader swaps and re-clamps to envelope
    const fs::path badPath = dir / "swapped.csv";
    {
        std::ofstream out(badPath);
        out << "open,high,low,close\n"
            << "100,95,105,102\n"
            << "102,106,101,104\n";
    }
    std::vector<OHLC> fixed;
    OB_CHECK(loadOhlcCsv(badPath, fixed, err));
    OB_CHECK(fixed.size() == 2);
    OB_CHECK(fixed[0].high >= fixed[0].low);
    OB_CHECK(fixed[0].high >= fixed[0].open && fixed[0].high >= fixed[0].close);
    OB_CHECK(fixed[0].low <= fixed[0].open && fixed[0].low <= fixed[0].close);

    fs::remove(csvPath);
    fs::remove(badPath);
}

}  // namespace

int main() {
    testDeterminism();
    testKeypointPinning();
    testOhlcValidityAllPacks();
    testBuiltinRegistry();
    testPackFileLoaderRoundTrip();
    testCsvLoader();
    std::puts("test_scenario: ALL PASSED");
}
