# Repository Guidelines

## Scope

This branch adds a gem5 SimObject for O3 RISC-V instruction classification and
attribution. Keep future work focused on
`src/cpu/probes/InstructionClassificationProbe.py`,
`src/cpu/probes/instruction_classification_probe.hh`,
`src/cpu/probes/instruction_classification_probe.cc`, and the related
`src/cpu/probes/SConscript` entry. Do not refactor unrelated CPU, stats, or
power-model code unless the probe cannot work without it.

## Component Structure

The Python SimObject declaration defines the attach point and configurable
energy coefficients:

```python
InstructionClassificationProbe(cpu=cpu)
```

The C++ implementation registers one commit probe listener per hardware thread,
filters committed architectural instructions, updates per-thread stats, and
keeps all state on the SimObject instance. The classifier lives in the isolated
`classify()` function. Preserve that separation when adding policies for
cache-missing memory ops, divides, or FP square roots.

## Build and Validation Commands

Always build with all local cores:

- `scons build/RISCV/cpu/probes/instruction_classification_probe.o -j$(nproc)`:
  fast compile check for this component.
- `scons build/RISCV/gem5.opt -j$(nproc)`: full RISC-V link check.
- `git diff --check`: whitespace and patch hygiene check.

For runtime validation, attach the probe before `m5.instantiate()` and inspect
`m5out/stats.txt` for `system.cpu.inst_classification.thread0.*`.

## Attribution Semantics

Instruction counts are retired-instruction counts from the O3 commit probe.
Execution cycles are attributed from each instruction's issue-to-complete
timestamps. Estimated energy and average power are derived from configured
per-cycle energy coefficients, not from gem5's aggregate CPU power model.
Document this distinction in any user-facing changes.

## Coding Style

Follow nearby gem5 C++ style: modern `gem5` namespace layout, `PARAMS(...)`,
`ADD_STAT`, and `ProbeListenerArgBase`. Use concise names matching existing
stats: `expensiveExecutionCycles`, `expensiveEstimatedEnergyPctOfTotal`, etc.
Python params use `snake_case`; generated stats use descriptive camelCase.

## Commit and PR Notes

Use component-tagged commit headers such as
`cpu: Add instruction attribution stats`. Include the build commands run and
note that long/expensive remain zero while the classifier stub returns normal.
Keep unrelated local changes, especially `SConstruct`, out of commits unless
explicitly requested.
