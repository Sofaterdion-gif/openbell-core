# The `.pack` scenario format

A `.pack` file is a plain-text, `key=value` description of one scenario. Drop `.pack`
files into a `scenarios/` folder next to your executable and they are picked up
automatically at runtime (`allPacks()`); duplicate ids are skipped with a load error.

Generation is **deterministic**: a pack with the same `id` always produces the same
bars on every platform and every run.

## Header keys

| key        | required | meaning |
|------------|----------|---------|
| `id`       | yes      | unique pack id (ASCII, no spaces recommended) |
| `title`    | yes      | display title |
| `blurb`    | no       | free text; use `\n` for line breaks |
| `bars`     | no       | note about what one bar means, e.g. `每根≈1 交易日` |
| `market`   | yes      | `US` `TW` `HK` `CRYPTO` `FUNDS` `FUTURES` |
| `currency` | yes      | e.g. `USD` `TWD` `HKD` `USDT` |
| `cash`     | yes      | starting cash for the scenario |

## Series keys — exactly 3 in total

### `symbol=` (keypoint-generated series)

```
symbol=NAME|noise|day:close[:high[:low]]],day:close[:high[:low]],...
```

- `NAME` — ticker/label.
- `noise` — AR(1) daily noise scale in log space, clamped to `[0, 0.3]`.
- Keypoints: `day` is the bar index (0–400), `close` the target close. Optional
  absolute `high` / `low` overrides force intraday extremes (e.g. an intraday wick).
- At least 2 keypoints per series. Between keypoints the path is log-linear
  interpolation plus pinned AR(1) noise — so closes hit every keypoint exactly.

### `ohlc=` (inject real historical CSV)

```
ohlc=NAME|file.csv
```

CSV rules (see `loadOhlcCsv` in `include/openbell/scenario.hpp`):

- Columns `open,high,low,close` (case-insensitive; single letters `o,h,l,c` also
  accepted). Optional leading `day` / `date` / `time` column is kept as the bar
  timestamp. Header optional — without one, 4 columns are assumed, 5+ columns take
  the first as the time index.
- Comma, tab, or semicolon separators. `#` lines are comments. Blank lines skipped.
- `high < low` rows are auto-swapped; the envelope is re-clamped so
  `high >= max(open,close)`, `low <= min(open,close)`, `low >= 0.01`.
- 2–2000 bars per file. Paths are relative to the `.pack` file's directory.

The 3 series may be any mix of `symbol=` and `ohlc=`.

## Complete example

```
# my_event.pack
id=MY_EVENT
title=My Custom Event
blurb=Three-act drawdown and recovery.\nUse it to practice dip buying.
bars=每根≈1 交易日，約 25 日
market=US
currency=USD
cash=10000

symbol=ABC|0.02|0:100,8:88,12:70,18:95,24:110
symbol=XYZ|0.03|0:50,8:44,12:35,18:48,24:55
ohlc=REALIDX|history.csv
```

## Programmatic use

```cpp
#include <openbell/scenario.hpp>

ScenarioPack p;
std::string err;
if (!loadPackFile("scenarios/my_event.pack", p, err)) {
    // err explains which line/key failed
}
```

Built-in packs live in the same structures — `allPacks()` returns bundled and
imported packs together (`packIsImported()` tells them apart).
