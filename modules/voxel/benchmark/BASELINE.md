# Phase 1 baseline

Recorded so Phase 2 can be diffed against it rather than argued about.

```
scons platform=windows target=editor
bin/godot.windows.editor.x86_64.console.exe --headless --path modules/voxel/benchmark
```

Only compare runs of the same build config on the same machine. An unoptimized
`dev_build=yes` binary is five times slower here than an optimized one, which is
more than any change this benchmark exists to detect, so a number without its
build config is worse than no number. The `BASELINE` line carries the executable
name for exactly that reason.

## Machine

12 logical cores, Windows, Godot 4.7.2-rc custom build.

## Workload

A 0.24 x 0.24 x 0.60 m block at 5 mm per voxel, so 48 x 48 x 120 cells across 72
regions. One box fill, then 24 subtractive strokes of 12 points each with a 12 mm
spherical tool, leaving 69.8% of the block and 61,140 triangles.

## Result

| Measurement | Optimized | `dev_build=yes` |
| --- | --- | --- |
| Fill | 9.7 ms | 59.9 ms |
| Stroke, median | 3.95 ms | 17.3 ms |
| Stroke, worst | 4.74 ms | 21.6 ms |
| Collision pass | 0.92 ms for 3,740 faces | 3.7 ms |
| CPU memory | 1.40 MiB | 1.40 MiB |

A stroke costs about 4 ms optimized, so Phase 1 is already inside a 60 Hz frame
budget on this workload with room to spare. The dev figure is recorded because it
is what `--test` and day-to-day work run against, and 17 ms there is not a
regression.

## What the numbers say

Roughly 3 of the 4 ms in a stroke is extraction, so extraction is where Phase 2
has to go.

The useful detail is that extraction barely moves with how much was actually
edited. A stroke that dirtied 3 of the 72 regions took 2.67 ms; one that dirtied
14 took 3.31 ms. If the cost were in meshing the dirty regions those two would
differ by more than four times. They do not, because meshing the dirty regions is
not the expensive part — rebuilding the whole `ArrayMesh` from every region's
output afterwards is, and that is paid in full no matter how little changed.

This is the cost the design knowingly accepted for Phase 1, and it is worth being
explicit that it is the only part that is not already local. The dirty set itself
is doing its job: 3 to 14 regions out of 72 per stroke is exactly the locality the
design claims, and it is why stamping is not already proportional to field size.

So the two things Phase 2 should be measured on:

1. **Incremental surface upload** instead of a full mesh rebuild. This should turn
   the flat ~2.6 ms floor into something proportional to the dirty region count,
   which on this workload would be several times faster for a small stroke and is
   the difference between a stroke costing 4 ms and costing 1.
2. **Memory under sparse storage.** 1.40 MiB is the dense cost of a block that is
   70% solid, which is the case dense storage is best at. This workload is
   deliberately not the one that shows the brick pool earning its place; a mostly
   empty field needs its own baseline when Phase 2 lands.

Collision at 0.92 ms for a quarter-resolution pass is not a problem and should be
left alone.
