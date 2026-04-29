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
- `configs/example/gpufs/mi300.py`: keeps a user-supplied CU count, including
  `--num-compute-units=...`, instead of forcing the MI300 default
- `util/amd_rodinia_stats.py`: maps serial `KERNEL`/`RESULT` lines to gem5
  per-kernel stats sections and summarizes CU cycles per benchmark result
- `util/amd_rodinia_opencl/`: the OpenCL Rodinia comparison harness and its
  checked-in v24-0-compatible binary

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

Use the project fork/branch below when reproducing MI300-capable resources:

```sh
git clone --branch compare-amd-mi300-resources \
  https://github.com/selimsandal/gem5-resources.git \
  /home/selimsandal/Developer/gem5-resources
```

That branch is based on the upstream gem5-resources MI300 update:

```text
1e7b2d7e resources: ROCm 6.4, Ubuntu 24.04 disk, MI300 roms
```

It contains the MI300 files needed by `configs/example/gpufs/mi300.py`:

```text
$GPUFS/files/mi300.rom
$GPUFS/files/mi300_discovery
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

The MI200 runs use the v24-0 disk as downloaded. For the MI300X runs in this
experiment, the exact artifact was the same v24-0 disk with the MI300 ROM and
IP-discovery table copied into it from the `compare-amd-mi300-resources`
branch. This avoids mixing today's results with a full Ubuntu 24.04/ROCm 6.4
disk rebuild.

Patch a copy of the v24-0 disk for MI300X:

```sh
export GPUFS_MI300_DISK=$GPUFS/disk-image/x86-ubuntu-gpu-ml-v24-mi300
cp "$GPUFS/disk-image/x86-ubuntu-gpu-ml" "$GPUFS_MI300_DISK"

docker run --rm --privileged \
  -v /home/selimsandal/Developer:/host \
  -v "$GPUFS/files:/mi300:ro" \
  ghcr.io/gem5/gpu-fs:v24-0 bash -lc '
set -euo pipefail
img=/host/gem5-resources/src/x86-ubuntu-gpu-ml/disk-image/x86-ubuntu-gpu-ml-v24-mi300
mnt=/mnt/gpuimg
mkdir -p "$mnt"
mount -o loop,offset=1048576 "$img" "$mnt"
trap "umount $mnt" EXIT
mkdir -p "$mnt/root/roms" "$mnt/usr/lib/firmware/amdgpu"
cp /mi300/mi300.rom "$mnt/root/roms/mi300.rom"
cp /mi300/mi300_discovery "$mnt/usr/lib/firmware/amdgpu/mi300_discovery"
cp /mi300/mi300_discovery "$mnt/usr/lib/firmware/amdgpu/ip_discovery.bin"
chmod 0644 \
  "$mnt/root/roms/mi300.rom" \
  "$mnt/usr/lib/firmware/amdgpu/mi300_discovery" \
  "$mnt/usr/lib/firmware/amdgpu/ip_discovery.bin"
sync
'
```

The local runs below were performed after patching
`$GPUFS/disk-image/x86-ubuntu-gpu-ml` in place. A copied disk prepared with the
commands above is preferred for clean reproduction.

## OpenCL Harness

The AMD OpenCL harness is checked into this gem5 branch so the AMD comparison
does not depend on the local GPU superrepo:

```text
util/amd_rodinia_opencl/amd_rodinia_opencl.cpp
util/amd_rodinia_opencl/Makefile
util/amd_rodinia_opencl/amd_rodinia_opencl
```

Build it with the v24-0 GPUFS container, not `latest`. The latest container
currently links against a newer glibc than the v24-0 Ubuntu 22.04 disk, which
causes guest load failures such as `GLIBC_2.38 not found`.

```sh
docker pull ghcr.io/gem5/gpu-fs:v24-0
docker run --rm -u "$(id -u):$(id -g)" \
  -v /home/selimsandal/Developer/gem5:/home/selimsandal/Developer/gem5 \
  -w /home/selimsandal/Developer/gem5/util/amd_rodinia_opencl \
  ghcr.io/gem5/gpu-fs:v24-0 make clean all

objdump -T util/amd_rodinia_opencl/amd_rodinia_opencl \
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

This is a throughput normalization, not an identical physical-lane match to
the FPGA. In this gem5 AMD model, `wf_size=64` means each vector instruction is
a 64-work-item AMD wavefront instruction. With `issue_period=4`, one SIMD
issues that 64-lane wavefront instruction over four shader cycles on average,
or `64 / 4 = 16` scalar lane-ops per cycle. With `simds_per_cu=2`, the CU
therefore has an average peak issue rate of `2 * 16 = 32` scalar lane-ops per
cycle.

The FPGA design's "32 execution cores" are treated here as 32 scalar work-item
slots available per cycle across its compute engines. The AMD configuration is
therefore comparable for peak scalar throughput at the same 100 MHz clock, but
it is not the same microarchitecture:

- The AMD model still executes and schedules wave64 work, so launch granularity,
  occupancy, barriers, and memory coalescing follow AMD wavefront behavior.
- The FPGA has its own physical engine/lane structure and dispatch granularity.
- The comparison should be described as "32 lane-op/cycle throughput-matched",
  not "the same 32 physical cores".

Changing `wf_size` to 32 would make the wavefront width look closer to a
32-lane machine, but it would no longer match the normal gfx90a/ROCm wave64
execution model used by this GPUFS setup. For the current comparison, keep
`wf_size=64` and use `issue_period`/`simds_per_cu` to normalize throughput.

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
export GPUFS_MI300_DISK=$GPUFS/disk-image/x86-ubuntu-gpu-ml-v24-mi300
export AMD_RODINIA=/home/selimsandal/Developer/gem5/util/amd_rodinia_opencl/amd_rodinia_opencl

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

export COMMON_GPUFS_MI300_ARGS="\
--disk-image $GPUFS_MI300_DISK \
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

MI300X smoke test:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-mi300-kmeans64-100mhz-lane32-smoke \
  configs/example/gpufs/mi300.py \
  $COMMON_GPUFS_MI300_ARGS \
  -o "kmeans 64"
```

MI300X validation-size suite:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-mi300-all-quick-100mhz-lane32 \
  configs/example/gpufs/mi300.py \
  $COMMON_GPUFS_MI300_ARGS \
  -o "all-quick"
```

MI300X larger timing suite:

```sh
./build/VEGA_X86/gem5.opt \
  -d m5out/compare-amd-rodinia-mi300-all-perf-100mhz-lane32 \
  configs/example/gpufs/mi300.py \
  $COMMON_GPUFS_MI300_ARGS \
  -o "all-perf"
```

Before accepting an MI300X run, verify that the 40-CU default was not used:

```sh
rg -c '^type=ComputeUnit$' m5out/<mi300-run>/config.ini
rg -n '^\\[system\\.cpu1\\.CUs|issue_period=|num_SIMDs=' \
  m5out/<mi300-run>/config.ini
```

The valid lane-matched runs should show one `ComputeUnit`,
`num_SIMDs=2`, and `issue_period=4`.

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

MI200 smoke run, compatibility and clock validation:

```text
m5out/compare-amd-rodinia-kmeans64-100mhz-smoke3
```

MI200 validation-size suite:

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

MI200 larger timing suite:

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

MI300X smoke run:

```text
m5out/compare-amd-rodinia-mi300-kmeans64-100mhz-lane32-smoke
```

MI300X validation-size suite:

```text
m5out/compare-amd-rodinia-mi300-all-quick-100mhz-lane32
```

Configuration checks from this run:

```text
ComputeUnit count: 1
issue_period=4
num_SIMDs=2
```

Summary from:

```sh
python3 util/amd_rodinia_stats.py \
  m5out/compare-amd-rodinia-mi300-all-quick-100mhz-lane32
```

| Result | Kernels | CU cycles | GPU ms @ 100 MHz | Completed WGs | Vec ops |
|---|---:|---:|---:|---:|---:|
| hotspot3D 32x32x8 | 1 | 250895 | 2.508950 | 128 | 375808 |
| gaussian Fan1 32x32 | 1 | 3390 | 0.033900 | 4 | 1375 |
| gaussian Fan2 32x32 | 1 | 47415 | 0.474150 | 128 | 44908 |
| gaussian full 32x32 | 62 | 638405 | 6.384050 | 1664 | 548944 |
| kmeans 64 points | 1 | 15259 | 0.152590 | 8 | 19648 |
| kmeans 128 points | 1 | 28976 | 0.289760 | 16 | 39296 |
| kmeans 256 points | 1 | 57435 | 0.574350 | 32 | 78592 |
| pathfinder 24 columns | 1 | 5183 | 0.051830 | 4 | 3372 |
| bfs 16 nodes | 6 | 14583 | 0.145830 | 12 | 2634 |
| streamcluster 16 points | 1 | 5818 | 0.058180 | 2 | 1826 |

MI300X larger timing suite:

```text
m5out/compare-amd-rodinia-mi300-all-perf-100mhz-lane32
```

Configuration checks from this run:

```text
ComputeUnit count: 1
issue_period=4
num_SIMDs=2
```

Summary from:

```sh
python3 util/amd_rodinia_stats.py \
  m5out/compare-amd-rodinia-mi300-all-perf-100mhz-lane32
```

| Result | Kernels | CU cycles | GPU ms @ 100 MHz | Completed WGs | Vec ops |
|---|---:|---:|---:|---:|---:|
| hotspot3D 128x128x8 | 1 | 3972926 | 39.729260 | 2048 | 6012928 |
| gaussian full 64x64 | 126 | 4371240 | 43.712400 | 12096 | 4118688 |
| kmeans 8192 points | 1 | 1836131 | 18.361310 | 1024 | 2514944 |
| pathfinder 1536 columns | 1 | 146313 | 1.463130 | 256 | 216816 |
| bfs 256 nodes | 18 | 130256 | 1.302560 | 576 | 101082 |
| streamcluster 1024 points | 1 | 184768 | 1.847680 | 128 | 116696 |

All `all-quick` and `all-perf` `RESULT` lines are `PASS`.

## MI200 Comparison Against FPGA Table

The FPGA values below are the `GPU ms` values already present in
`/home/selimsandal/Developer/gpu/External/paper/main.tex`. The AMD MI200
values are from gem5 `CUs.totalCycles` at 100 MHz. `MI200/FPGA` below 1.0
means the AMD gem5 model reported fewer GPU milliseconds than the FPGA run.

| Benchmark | Workload | FPGA GPU ms | MI200 GPU ms | MI200/FPGA |
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

## MI300X Comparison Against FPGA Table

The MI300X rows use the same 32 lane-op/cycle throughput match and 100 MHz
clock options. They differ from the MI200 rows only by the gem5 board script,
ROM/IP-discovery resources, and the MI300X GPU identity seen by the guest.

| Benchmark | Workload | FPGA GPU ms | MI300X GPU ms | MI300X/FPGA |
|---|---:|---:|---:|---:|
| hotspot3D | 32x32x8 | 4.101 | 2.508950 | 0.612 |
| hotspot3D | 128x128x8 | 65.325 | 39.729260 | 0.608 |
| gaussian Fan1 | 32x32 | 0.012 | 0.033900 | 2.825 |
| gaussian Fan2 | 32x32 | 0.532 | 0.474150 | 0.891 |
| gaussian full | 32x32 | 7.054 | 6.384050 | 0.905 |
| gaussian full | 64x64 | 49.650 | 43.712400 | 0.880 |
| kmeans | 64 points | 0.136 | 0.152590 | 1.122 |
| kmeans | 128 points | 0.272 | 0.289760 | 1.065 |
| kmeans | 256 points | 0.546 | 0.574350 | 1.052 |
| kmeans | 8192 points | 17.468 | 18.361310 | 1.051 |
| pathfinder | 24 columns | 0.030 | 0.051830 | 1.728 |
| pathfinder | 1536 columns | 1.912 | 1.463130 | 0.765 |
| bfs | 16 nodes | 0.148 | 0.145830 | 0.985 |
| bfs | 256 nodes | 2.009 | 1.302560 | 0.648 |
| streamcluster | 16 points | 0.039 | 0.058180 | 1.492 |
| streamcluster | 1024 points | 1.289 | 1.847680 | 1.433 |

## MI200 Versus MI300X

This table compares the two AMD gem5 models under the same lane-matched
configuration. `MI300X/MI200` below 1.0 means the MI300X-script run reported
fewer GPU milliseconds than the MI200-script run.

| Benchmark | Workload | MI200 GPU ms | MI300X GPU ms | MI300X/MI200 |
|---|---:|---:|---:|---:|
| hotspot3D | 32x32x8 | 2.853390 | 2.508950 | 0.879 |
| hotspot3D | 128x128x8 | 45.078820 | 39.729260 | 0.881 |
| gaussian Fan1 | 32x32 | 0.029120 | 0.033900 | 1.164 |
| gaussian Fan2 | 32x32 | 0.529430 | 0.474150 | 0.896 |
| gaussian full | 32x32 | 7.024760 | 6.384050 | 0.909 |
| gaussian full | 64x64 | 48.413770 | 43.712400 | 0.903 |
| kmeans | 64 points | 0.161620 | 0.152590 | 0.944 |
| kmeans | 128 points | 0.307950 | 0.289760 | 0.941 |
| kmeans | 256 points | 0.613040 | 0.574350 | 0.937 |
| kmeans | 8192 points | 19.672380 | 18.361310 | 0.933 |
| pathfinder | 24 columns | 0.056820 | 0.051830 | 0.912 |
| pathfinder | 1536 columns | 1.492520 | 1.463130 | 0.980 |
| bfs | 16 nodes | 0.146880 | 0.145830 | 0.993 |
| bfs | 256 nodes | 1.642610 | 1.302560 | 0.793 |
| streamcluster | 16 points | 0.061770 | 0.058180 | 0.942 |
| streamcluster | 1024 points | 1.958560 | 1.847680 | 0.943 |

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
- The MI300X guest driver can report many discovered rings/CUs from firmware
  discovery even when the gem5 compute model has been reduced. Accept the
  lane-matched MI300X results only after `config.ini` shows one `ComputeUnit`,
  `num_SIMDs=2`, and `issue_period=4`.
