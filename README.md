# Heatwave Hunter

Data Structures lab mini-project · AI use case **KJS-CES-01 — Climate Intelligence for Heatwave Monitoring, Prediction and Early Warning**

Real daily maximum-temperature data from **IMD Pune (2015–2025)**, run through all eight lab experiments.
Every operation is written in **C with no standard library** and compiled to **WebAssembly**; the
JavaScript only draws what the C code returns.

## What it does

| Screen | What you see |
|---|---|
| **Tracker** | India's 1° grid for any day, 1 March – 30 June, 2015–2025. Heatwave regions are found and labelled automatically (`HW-01`, …). Click a cell for its 14-day history. Write your own alert rule. Years ranked. Incoming-day queue. Watchlist. |
| **Search** | `above 45 on 28-05-2024`, `hottest 10`, `hottest years`, `find delhi`, `spread from churu`, `where T >= 46 && days >= 3`. Shows the plan (which structure answered) and a race of comparison counts measured in C. |
| **Lab** | Every function from Exp 1–8 with its own inputs, a live drawing of the structure and the real C source. |
| **Live function log** | Every call the C core makes, filterable by experiment. Click a line to see that function's code. |
| **X-ray** | Labels every panel with the data structure behind it. |

## Where each experiment is used

| Exp | Structure | Job | File |
|---|---|---|---|
| 1 | Array of structs, pointers | `Cell grid[31][31]` for the day; `getCell()` returns `&grid[r][c]` | `core/grid.c` |
| 2 | Singly linked list | Watchlist (`insertBegin`, `insertAfter`, `deleteBefore`, `display`) and each cell's history | `core/list.c` |
| 3 | Stack on a linked list | Alert rule / `where` search: infix → postfix → evaluate | `core/stack.c` |
| 4 | Circular queue (counter method) | Incoming-day feed, BFS queue, the log itself | `core/queue.c`, `core/trace.c` |
| 5 | Binary search tree | Cells keyed by temperature; `rangeSearch` for "≥ 45 °C" | `core/bst.c` |
| 6 | Graph (adjacency matrix) + BFS | Touching land cells; BFS finds heatwave regions and spread | `core/graph.c` |
| 7 | Sorting + binary search | Regions, cells and years ranked (quick, merge, insertion); binary search | `core/sort.c` |
| 8 | Hash table, circular array, linear probing | City name → grid cell | `core/hash.c` |

`core/app.c` connects them and returns JSON; `core/mem.c` replaces the libc pieces we need
(`memcpy`, string helpers, number formatting).

## Run it locally

```sh
cd public
python3 -m http.server 8080      # then open http://localhost:8080
```
(Opening `index.html` directly from disk won't work: browsers block loading `core.wasm` from `file://`.)

## Rebuild from source

```sh
sh build.sh
```
Needs `gcc` and `clang` with the `wasm32` target (`wasm-ld`). The script:
1. compiles `tools/prep.c` and packs IMD's `.GRD` files from `data/` into `public/data/season.bin`
2. builds and runs the native tests (`tests/test_core.c`) against the same C files
3. compiles `core/*.c` to `public/core.wasm` with `-nostdlib`
4. copies the C files into `public/src/` for Lab mode

## Deploy on Vercel

Push this folder to GitHub and import it in Vercel. `vercel.json` serves `public/` as a static
site with no build step, so the committed `core.wasm` and `season.bin` are used as they are.

## About the data

- IMD Pune, *Yearly Gridded Maximum Temperature (1.0° × 1.0°)*, binary, one file per year:
  31 × 31 floats per day, latitude 7.5–37.5 °N, longitude 67.5–97.5 °E. https://www.imdpune.gov.in/cmpg/Griddata/Max_1_Bin.html
- Only 1 March – 30 June and the 355 cells over India are kept (≈ 950 KB for 11 years).
- IMD writes 99.9 for a missing reading; those cell-days are skipped, never counted as heat.
- Grid cells are averages, so peaks (≈ 48–49 °C) are lower than single-station records.
- From 2008 the grids use about 180 stations, so 2015–2025 compare on equal terms.
- Heatwave levels follow IMD's actual-temperature rule for the plains (≥ 45 °C, ≥ 47 °C severe);
  the threshold slider lets you change it.
- Please cite: Srivastava, A. K., Rajeevan, M., Kshirsagar, S. R. (2009). *Development of a high
  resolution daily gridded temperature data set (1969–2005) for the Indian region.*
  Atmospheric Science Letters. DOI 10.1002/asl.232

## Team

- Sanyog Pardeshi · 16010425081 · A3

