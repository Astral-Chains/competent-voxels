# Phase 1 baseline

Recorded so Phase 2 can be diffed against it rather than argued about.

```
scons platform=windows target=editor
bin/godot.windows.editor.x86_64.console.exe --headless --path modules/voxel/benchmark
```

The harness runs the workload five times and reports the best. Interference can
only make a run slower, never faster, so the minimum is the number that says
something about the code. The `BASELINE` line also carries `spread_pct`: if that
is large, treat the figure as an upper bound and do not read small differences
into it.

Only compare runs of the same build config on the same machine. An unoptimized
`dev_build=yes` binary is several times slower here than an optimized one, which
is more than any change this benchmark exists to detect. The `BASELINE` line
carries the executable name for exactly that reason.

## Machine

12 logical cores, Windows, Godot 4.7.2-rc custom build.

## Workload

A 0.24 x 0.24 x 0.60 m block at 5 mm per voxel, so 48 x 48 x 120 cells across 72
regions. One box fill, then 24 subtractive strokes of 12 points each with a 12 mm
spherical tool, leaving 69.8% of the block and 61,140 triangles.

## Result

Best of 5 runs, optimized editor build:

| Measurement | Value |
| --- | --- |
| Fill | 8.5 ms |
| Stroke, median | 3.19 ms |
| Stroke, worst | 5.86 ms |
| Collision pass | 0.81 ms for 3,740 faces |
| CPU memory | 1.40 MiB |
| Run spread | 12.8% |

A stroke costs about 3 ms optimized, so Phase 1 is already inside a 60 Hz frame
budget on this workload with room to spare. A `dev_build=yes` binary is several
times slower and is what `--test` and day-to-day work run against; that is not a
regression, just an unoptimized build.

## What the numbers say

Roughly three quarters of a stroke is extraction, so extraction is where Phase 2
has to go.

The useful detail is that extraction barely moves with how much was actually
edited. A stroke that dirtied 3 of the 72 regions and one that dirtied 14 stay
within a few milliseconds of each other. If the cost were in meshing the dirty
regions those two would differ by more than four times. They do not, because
meshing the dirty regions is not the expensive part — rebuilding the whole
`ArrayMesh` from every region's output afterwards is, and that is paid in full no
matter how little changed.

This is the cost the design knowingly accepted for Phase 1, and it is worth being
explicit that it is the only part that is not already local. The dirty set itself
is doing its job: 3 to 14 regions out of 72 per stroke is exactly the locality the
design claims, and it is why stamping is not already proportional to field size.

So the two things Phase 2 should be measured on:

1. **Incremental surface upload** instead of a full mesh rebuild. This should turn
   the flat extraction floor into something proportional to the dirty region
   count, which on this workload would be several times faster for a small stroke
   and is the difference between a stroke costing ~3 ms and costing ~1.
2. **Memory under sparse storage.** 1.40 MiB is the dense cost of a block that is
   70% solid, which is the case dense storage is best at. This workload is
   deliberately not the one that shows the brick pool earning its place; a mostly
   empty field needs its own baseline when Phase 2 lands.

Collision at under a millisecond for a quarter-resolution pass is not a problem
and should be left alone.
