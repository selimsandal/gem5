# Repository Guidelines

## Project Structure

This branch adds an O3 RISC-V instruction-classification SimObject. Keep probe
work scoped to `src/cpu/probes/InstructionClassificationProbe.py`,
`src/cpu/probes/instruction_classification_probe.hh`,
`src/cpu/probes/instruction_classification_probe.cc`, and
`src/cpu/probes/SConscript`. The generic SE-mode config lives in
`configs/example/riscv_app_probe.py`. Quick local workloads live outside the
repo at `~/Developer/riscv-se-samples`.

## Probe Behavior

Attach the probe to each O3 CPU before `m5.instantiate()`:

```python
InstructionClassificationProbe(cpu=cpu)
```

The C++ code registers one commit listener per hardware thread, filters
architectural commits, and updates per-thread stats. Keep classification inside
`classify()`. Current policy marks divide/square-root op classes as
`expensive`; memory op classes become `longLatency` when issue-to-complete
cycles are at least `long_latency_min_cycles` (default 1). Replace that memory
proxy with true cache-miss metadata when available.

## Build and Validation Commands

Always build with all local cores:

- `scons build/RISCV/cpu/probes/instruction_classification_probe.o -j$(nproc)`:
  fast compile check for this component.
- `scons build/RISCV/gem5.opt -j$(nproc)`: full RISC-V link check.
- `git diff --check`: whitespace and patch hygiene check.

Build sample apps with `make -C ~/Developer/riscv-se-samples -j$(nproc)`.
Useful binaries are `div_loop` for `expensive`, `long_loads` for `longLatency`,
and `mixed_long_expensive` for both. Inspect `m5out/stats.txt` for
`system.cpu.inst_classification.thread0.*`.

## RISC-V App Runtime Setup

The example config runs any RISC-V SE-mode app, with Geekbench as the default.
Dynamically linked RISC-V apps need a runtime sysroot on x86 hosts:

```bash
sudo apt install --no-install-recommends libc6-riscv64-cross libgcc-s1-riscv64-cross
```

Run through gem5 without overriding the output directory:

```bash
build/RISCV/gem5.opt configs/example/riscv_app_probe.py
```

Override the default app with `--app-dir`, `--app-binary`, `--app-args`,
`--app-cwd`, or `--riscv-sysroot`. You can also pass gem5 SE options directly
with `--cmd` and `--options`. On native RISC-V hosts, the config skips loader
redirects and lets SE mode use native RISC-V libraries.

## Attribution Semantics

Instruction counts are retired instructions from the O3 commit probe. Execution
cycles use issue-to-complete timestamps. Estimated energy and average power come
from configured per-cycle energy coefficients, not gem5's aggregate CPU power
model. Document that distinction in user-facing changes.

## Coding Style

Follow nearby gem5 C++ style: modern `gem5` namespace layout, `PARAMS(...)`,
`ADD_STAT`, and `ProbeListenerArgBase`. Use stat names like
`expensiveExecutionCycles` and `expensiveEstimatedEnergyPctOfTotal`. Python
params use `snake_case`; generated stats use descriptive camelCase.

## Commit and PR Notes

Use component-tagged commit headers such as `cpu: Add instruction attribution
stats`. Include build commands and sample binaries used for validation. Keep
unrelated local changes, especially `SConstruct`, out of commits unless
explicitly requested.
