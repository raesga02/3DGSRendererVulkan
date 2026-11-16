# Desktop measurements — all scenes

1600x900, `--profile --noidle` (sort rebuilt every frame), RTX 5070 Laptop.

Tiled columns: project/bin/sort/tiles/composite. Instanced: keygen/radix/raster.

| Scene | Inst total | Inst FPS | Tiled total | Tiled FPS | Tiled vs inst | Raster inst | Tiles tiled | Tile saving |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bicycle | 20.09 | 50 | 26.41 | 38 | +31.5% | 11.35 | 9.72 | 1.63 ms |
| garden | 16.67 | 60 | 21.31 | 47 | +27.8% | 9.89 | 9.22 | 0.67 ms |
| stump | 13.96 | 72 | 16.52 | 61 | +18.3% | 8.96 | 6.77 | 2.19 ms |
| kitchen | 11.54 | 87 | 13.76 | 73 | +19.2% | 9.48 | 5.99 | 3.49 ms |
| truck | 9.68 | 103 | 11.95 | 84 | +23.5% | 7.36 | 5.02 | 2.34 ms |
| treehill | 9.47 | 106 | 11.22 | 89 | +18.5% | 6.19 | 4.43 | 1.76 ms |
| counter | 8.65 | 116 | 10.07 | 99 | +16.4% | 7.54 | 4.35 | 3.19 ms |
| room | 8.40 | 119 | 10.63 | 94 | +26.5% | 6.92 | 4.97 | 1.95 ms |
| flowers | 7.75 | 129 | 9.88 | 101 | +27.5% | 4.65 | 4.02 | 0.63 ms |
| drjohnson | 7.57 | 132 | 8.44 | 118 | +11.5% | 5.03 | 3.84 | 1.19 ms |
| train | 7.54 | 133 | 8.15 | 123 | +8.1% | 6.53 | 3.19 | 3.34 ms |
| playroom | 7.36 | 136 | 9.62 | 104 | +30.7% | 5.33 | 4.35 | 0.98 ms |
| bonsai | 4.52 | 221 | 6.55 | 153 | +44.9% | 3.50 | 2.61 | 0.89 ms |

## Pass breakdown

| Scene | Inst keygen | Inst radix | Inst raster | Tiled project | Tiled bin | Tiled sort | Tiled tiles | Tiled comp |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bicycle | 3.35 | 5.33 | 11.35 | 7.20 | 1.71 | 7.75 | 9.72 | 0.03 |
| garden | 1.63 | 5.10 | 9.89 | 4.32 | 1.05 | 6.69 | 9.22 | 0.03 |
| stump | 1.38 | 3.57 | 8.96 | 3.19 | 1.04 | 5.48 | 6.77 | 0.02 |
| kitchen | 0.54 | 1.47 | 9.48 | 1.96 | 0.96 | 4.82 | 5.99 | 0.02 |
| truck | 0.73 | 1.54 | 7.36 | 1.86 | 0.91 | 4.13 | 5.02 | 0.02 |
| treehill | 1.10 | 2.13 | 6.19 | 2.37 | 0.80 | 3.60 | 4.43 | 0.02 |
| counter | 0.38 | 0.69 | 7.54 | 1.08 | 0.80 | 3.82 | 4.35 | 0.02 |
| room | 0.47 | 0.96 | 6.92 | 1.24 | 0.80 | 3.60 | 4.97 | 0.02 |
| flowers | 1.05 | 2.01 | 4.65 | 2.28 | 0.61 | 2.95 | 4.02 | 0.02 |
| drjohnson | 0.98 | 1.52 | 5.03 | 1.45 | 0.53 | 2.59 | 3.84 | 0.02 |
| train | 0.33 | 0.63 | 6.53 | 1.17 | 0.69 | 3.07 | 3.19 | 0.02 |
| playroom | 0.73 | 1.26 | 5.33 | 1.54 | 0.69 | 3.01 | 4.35 | 0.02 |
| bonsai | 0.37 | 0.60 | 3.50 | 1.48 | 0.56 | 1.87 | 2.61 | 0.02 |

## Observations

- Tiled tile raster is faster than instanced raster in all 13 scenes; mean saving 1.87 ms (median 1.76 ms).
- Tiled total is slower in all 13 scenes; mean overhead +23.4% (median +23.5%).
- Closest tiled totals: train (8.15 vs 7.54 ms, +8.1%), drjohnson (8.44 vs 7.57 ms, +11.5%), counter (10.07 vs 8.65 ms, +16.4%).
- Worst relative tiled overhead: bonsai (+44.9%), bicycle (+31.5%), playroom (+30.7%).
- The tiled path duplicates each Gaussian into every overlapping tile, so its project/bin/sort entry count is much larger than the instanced depth sort.
