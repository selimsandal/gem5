# AMD GPUFS Rodinia Comparison

This note documents the reproducible path for comparing the local FPGA
Rodinia runs against gem5's AMD GPU full-system model on branch
`compare-amd`.

The FPGA table being matched is in
`/home/selimsandal/Developer/gpu/External/paper/main.tex`. Do not edit the
paper while collecting these runs.

## Branch And Build

```sh
cd /home/selimsandal/Developer/gem5
git switch compare-amd
git status --short --branch
scons -j"$(nproc)" build/VEGA_X86/gem5.opt
```

The comparison branch includes:

- `configs/example/gpufs/runfs.py`: `--dump-reset-on-gpu-kernel`
- `configs/example/gpufs/system/system.py`: dispatcher kernel completion
  exits for dump/reset
- `configs/example/gpufs/mi300.py`: keeps a user-supplied CU count instead of
  forcing the MI300 default
- `util/amd_rodinia_stats.py`: maps serial `KERNEL`/`RESULT` lines to gem5
  per-kernel stats sections and summarizes CU cycles per benchmark result

The first pushed comparison commit is:

```text
7341741f28 configs: Add GPUFS comparison stats hooks
```

## GPUFS Resources

Resource directory:

```sh
export GEM5_RES=/home/selimsandal/Developer/gem5-resources
export GPUFS=$GEM5_RES/src/x86-ubuntu-gpu-ml
```

Disk and kernel paths used by the runs:

```text
$GPUFS/disk-image/x86-ubuntu-gpu-ml
$GPUFS/vmlinux-gpu-ml
```

The v24-0 resources were fetched from:

```text
https://dist.gem5.org/dist/v24-0/gpu-fs/kernel/vmlinux-gpu-ml.gz
https://dist.gem5.org/dist/v24-0/gpu-fs/diskimage/x86-ubuntu-gpu-ml.gz
```

For a clean parallel disk download:

```sh
mkdir -p "$GPUFS/disk-image"
aria2c -x16 -s16 -k1M --summary-interval=10 --file-allocation=none \
  --allow-overwrite=true \
  --dir="$GPUFS/disk-image" \
  --out=x86-ubuntu-gpu-ml.gz \
  https://dist.gem5.org/dist/v24-0/gpu-fs/diskimage/x86-ubuntu-gpu-ml.gz
gzip -dk "$GPUFS/disk-image/x86-ubuntu-gpu-ml.gz"
```

The kernel can be decompressed with:

```sh
curl -L \
  https://dist.gem5.org/dist/v24-0/gpu-fs/kernel/vmlinux-gpu-ml.gz \
  -o "$GPUFS/vmlinux-gpu-ml.gz"
gzip -dk "$GPUFS/vmlinux-gpu-ml.gz"
```

## OpenCL Harness

The AMD OpenCL harness lives in the GPU repo:

```text
/home/selimsandal/Developer/gpu/Source/Host/Tests/RodiniaAmd/amd_rodinia_opencl
```

Build it with the v24-0 GPUFS container, not `latest`. The latest container
currently links against a newer glibc than the v24-0 Ubuntu 22.04 disk, which
causes guest load failures such as `GLIBC_2.38 not found`.

```sh
docker pull ghcr.io/gem5/gpu-fs:v24-0
docker run --rm -u "$(id -u):$(id -g)" \
  -v /home/selimsandal/Developer/gpu:/home/selimsandal/Developer/gpu \
  -w /home/selimsandal/Developer/gpu/Source/Host/Tests/RodiniaAmd \
  ghcr.io/gem5/gpu-fs:v24-0 make clean all

objdump -T /home/selimsandal/Developer/gpu/Source/Host/Tests/RodiniaAmd/amd_rodinia_opencl \
  | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail
```

The rebuilt binary should require glibc no newer than the Ubuntu 22.04 guest
provides. In the current run it requires up to `GLIBC_2.34`.

## AMD Model Configuration

The target comparison configuration approximates 32 scalar lane-ops per shader
cycle:

```text
num_compute_units * simds_per_cu * wf_size / issue_period
= 1 * 2 * 64 / 4
= 32
```

Use these gem5 options:

```text
--num-compute-units=1
--simds-per-cu=2
--issue-period=4
--wf-size=64
```

Clock matching requires all of these options:

```text
--sys-clock=100MHz
--ruby-clock=100MHz
--gpu-clock=100MHz
```

`--gpu-clock` configures the GPU TLB clock hierarchy in this GPUFS path.
The shader/CU objects inherit `system.clk_domain`, so `--sys-clock=100MHz`
is required for 100 MHz CU cycle accounting.

Confirm after a run:

```sh
grep -n '"path": "system.clk_domain"' -A5 m5out/<run>/config.json
grep -n '"path": "system.l1_coalescer.clk_domain"' -A5 m5out/<run>/config.json
```

Both should show `clock: [10000]`, because gem5 uses a 1 THz tick and
100 MHz is 10,000 ticks per cycle.

## Workloads

Validation-size set matching the FPGA table:

```text
hotspot3D       32x32x8 cells
gaussian Fan1   32x32, one pivot
gaussian Fan2   32x32, one update
gaussian full   32x32
kmeans          64 points
kmeans          128 points
kmeans          256 points
pathfinder      24 columns, one step
bfs             16 nodes
streamcluster   16 points
```

Larger timing set matching the FPGA table:

```text
hotspot3D       128x128x8 cells
gaussian full   64x64
kmeans          8192 points
pathfinder      1536 columns, one step
bfs             256 nodes
streamcluster   1024 points
```

## Run Commands

Common variables:

```sh
cd /home/selimsandal/Developer/gem5
export GEM5_RES=/home/selimsandal/Developer/gem5-resources
export GPUFS=$GEM5_RES/src/x86-ubuntu-gpu-ml
export AMD_RODINIA=/home/selimsandal/Developer/gpu/Source/Host/Tests/RodiniaAmd/amd_rodinia_opencl

export COMMON_GPUFS_ARGS="\
--disk-image $GPUFS/disk-image/x86-ubuntu-gpu-ml \
--kernel $GPUFS/vmlinux-gpu-ml \
--app $AMD_RODINIA \
--num-compute-units=1 \
--simds-per-cu=2 \
--issue-period=4 \
--sys-clock=100MHz \
--ruby-clock=100MHz \
--gpu-clock=100MHz \
--dump-reset-on-gpu-kernel"
```

Smoke test:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-kmeans64-100mhz-smoke \
  configs/example/gpufs/mi200.py \
  $COMMON_GPUFS_ARGS \
  -o "kmeans 64"
```

Validation-size suite:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-all-quick-100mhz \
  configs/example/gpufs/mi200.py \
  $COMMON_GPUFS_ARGS \
  -o "all-quick"
```

Larger timing suite:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-all-perf-100mhz \
  configs/example/gpufs/mi200.py \
  $COMMON_GPUFS_ARGS \
  -o "all-perf"
```

Individual runs are useful when mapping a single benchmark to stats:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-hotspot3d-32-100mhz \
  configs/example/gpufs/mi200.py \
  $COMMON_GPUFS_ARGS \
  -o "hotspot3D 32 32 8"
```

## Result Extraction

Guest correctness and OpenCL event timing are in the serial log:

```sh
grep -E 'OpenCL device:|^KERNEL |^RESULT ' \
  m5out/<run>/system.pc.com_1.device
```

Use the `RESULT ... PASS/FAIL` fields for correctness. Do not use the
reported `event_ms` as the comparison timing metric for these GPUFS runs. The
simulation emits timestamp-related warnings such as `SDMA_OP_TIMESTAMP not
implemented`, and the OpenCL profiling values observed so far do not agree
with the CU-cycle stats. Use gem5 stats below for timing.

Per-kernel gem5 stats are in `stats.txt`. The comparison branch resets stats
after each GPU kernel completion, so each section has one completed GPU task:

```sh
grep -n 'Begin Simulation Statistics\|End Simulation Statistics' \
  m5out/<run>/stats.txt

grep -E 'simTicks|simSeconds|shaderActiveTicks|CUs\.totalCycles|CUs\.numVecOpsExecuted|CUs\.vpc|CUs\.ipc|CUs\.completedWGs|numKernelLaunched' \
  m5out/<run>/stats.txt
```

For 100 MHz runs:

```text
cycles = system.cpu1.CUs.totalCycles
cycles = system.cpu1.shaderActiveTicks / 10000
gpu_ms = cycles / 100000.0
```

The OpenCL runtime can launch setup kernels before the first benchmark kernel.
Those setup sections are visible in `stats.txt` but do not correspond to a
`KERNEL ...` line from the harness. For clean final tables, prefer individual
benchmark runs, then map user kernels by launch order and `completedWGs`.

The helper parser automates that mapping for the current harness:

```sh
python3 util/amd_rodinia_stats.py m5out/compare-amd-rodinia-all-quick-100mhz
python3 util/amd_rodinia_stats.py m5out/compare-amd-rodinia-all-quick-100mhz --kernels
```

The default parser setting drops the first two ROCm/OpenCL runtime warmup
kernels with `--skip-warmup=2`. For the `all-quick` run this produced:

```text
sections=79 usable_kernel_sections=78 serial_kernels=76 skip_warmup=2
```

## Current Run Artifacts

Smoke run, compatibility and clock validation:

```text
m5out/compare-amd-rodinia-kmeans64-100mhz-smoke3
```

Validation-size suite:

```text
m5out/compare-amd-rodinia-all-quick-100mhz
```

Summary from:

```sh
python3 util/amd_rodinia_stats.py m5out/compare-amd-rodinia-all-quick-100mhz
```

| Result | Kernels | CU cycles | GPU ms @ 100 MHz | Completed WGs | Vec ops |
|---|---:|---:|---:|---:|---:|
| hotspot3D 32x32x8 | 1 | 285339 | 2.853390 | 128 | 483328 |
| gaussian Fan1 32x32 | 1 | 2912 | 0.029120 | 4 | 1499 |
| gaussian Fan2 32x32 | 1 | 52943 | 0.529430 | 128 | 53991 |
| gaussian full 32x32 | 62 | 702476 | 7.024760 | 1664 | 651616 |
| kmeans 64 points | 1 | 16162 | 0.161620 | 8 | 22528 |
| kmeans 128 points | 1 | 30795 | 0.307950 | 16 | 45056 |
| kmeans 256 points | 1 | 61304 | 0.613040 | 32 | 90112 |
| pathfinder 24 columns | 1 | 5682 | 0.056820 | 4 | 3630 |
| bfs 16 nodes | 6 | 14688 | 0.146880 | 12 | 3014 |
| streamcluster 16 points | 1 | 6177 | 0.061770 | 2 | 2023 |

Larger timing suite:

```text
m5out/compare-amd-rodinia-all-perf-100mhz
```

Summary from:

```sh
python3 util/amd_rodinia_stats.py m5out/compare-amd-rodinia-all-perf-100mhz
```

| Result | Kernels | CU cycles | GPU ms @ 100 MHz | Completed WGs | Vec ops |
|---|---:|---:|---:|---:|---:|
| hotspot3D 128x128x8 | 1 | 4507882 | 45.078820 | 2048 | 7733248 |
| gaussian full 64x64 | 126 | 4841377 | 48.413770 | 12096 | 4923072 |
| kmeans 8192 points | 1 | 1967238 | 19.672380 | 1024 | 2883584 |
| pathfinder 1536 columns | 1 | 149252 | 1.492520 | 256 | 233706 |
| bfs 256 nodes | 18 | 164261 | 1.642610 | 576 | 110414 |
| streamcluster 1024 points | 1 | 195856 | 1.958560 | 128 | 129325 |

All `all-quick` and `all-perf` `RESULT` lines are `PASS`.

## Preliminary Comparison Against FPGA Table

The FPGA values below are the `GPU ms` values already present in
`/home/selimsandal/Developer/gpu/External/paper/main.tex`. The AMD values are
from gem5 `CUs.totalCycles` at 100 MHz. `AMD/FPGA` below 1.0 means the AMD
gem5 model reported fewer GPU milliseconds than the FPGA run.

| Benchmark | Workload | FPGA GPU ms | AMD GPU ms | AMD/FPGA |
|---|---:|---:|---:|---:|
| hotspot3D | 32x32x8 | 4.101 | 2.853390 | 0.696 |
| hotspot3D | 128x128x8 | 65.325 | 45.078820 | 0.690 |
| gaussian Fan1 | 32x32 | 0.012 | 0.029120 | 2.427 |
| gaussian Fan2 | 32x32 | 0.532 | 0.529430 | 0.995 |
| gaussian full | 32x32 | 7.054 | 7.024760 | 0.996 |
| gaussian full | 64x64 | 49.650 | 48.413770 | 0.975 |
| kmeans | 64 points | 0.136 | 0.161620 | 1.188 |
| kmeans | 128 points | 0.272 | 0.307950 | 1.132 |
| kmeans | 256 points | 0.546 | 0.613040 | 1.123 |
| kmeans | 8192 points | 17.468 | 19.672380 | 1.126 |
| pathfinder | 24 columns | 0.030 | 0.056820 | 1.894 |
| pathfinder | 1536 columns | 1.912 | 1.492520 | 0.781 |
| bfs | 16 nodes | 0.148 | 0.146880 | 0.992 |
| bfs | 256 nodes | 2.009 | 1.642610 | 0.818 |
| streamcluster | 16 points | 0.039 | 0.061770 | 1.584 |
| streamcluster | 1024 points | 1.289 | 1.958560 | 1.520 |

## Known Pitfalls

- Building the harness with `ghcr.io/gem5/gpu-fs:latest` produced a
  `GLIBC_2.38 not found` failure in the v24-0 disk. Use
  `ghcr.io/gem5/gpu-fs:v24-0`.
- The standard HIP `square.default` smoke binary built in the latest container
  failed on the v24-0 disk because it needed `libamdhip64.so.7`, while the
  disk is ROCm 6.1 era. The OpenCL harness avoids that HIP major-version
  mismatch when built with the v24-0 container.
- The guest driver prints `active_cu_number 112` for the emulated MI200 device.
  The gem5 compute model used for timing is still controlled by
  `--num-compute-units`, `--simds-per-cu`, and `--issue-period`; verify via
  stats and the run command, not that driver discovery string.
