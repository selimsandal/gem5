#!/usr/bin/env python3

import argparse
import math
import re
import sys
from pathlib import Path

STAT_KEYS = {
    "simSeconds": "sim_seconds",
    "simTicks": "sim_ticks",
    "shaderActiveTicks": "shader_active_ticks",
    "CUs.totalCycles": "cu_cycles",
    "CUs.completedWGs": "completed_wgs",
    "CUs.numVecOpsExecuted": "vec_ops",
    "CUs.vpc": "vpc",
    "CUs.ipc": "ipc",
    "gpu_cmd_proc.dispatcher.numKernelLaunched": "kernels_launched",
}


KERNEL_RE = re.compile(r"^KERNEL\s+(\S+)\s+event_ms=([0-9.]+)")
RESULT_RE = re.compile(r"^RESULT\s+(.+)$")


def parse_value(text):
    if text.lower() == "nan":
        return math.nan
    if "." in text or "e" in text.lower():
        return float(text)
    return int(text)


def stat_key(name):
    if name in ("simSeconds", "simTicks"):
        return STAT_KEYS[name]
    for suffix, key in STAT_KEYS.items():
        if suffix in ("simSeconds", "simTicks"):
            continue
        if name.endswith(suffix):
            return key
    return None


def parse_stats(stats_path):
    sections = []
    current = None
    for line in stats_path.read_text(errors="replace").splitlines():
        if line.startswith("---------- Begin Simulation Statistics"):
            current = {"section": len(sections)}
            continue
        if line.startswith("---------- End Simulation Statistics"):
            if current is not None:
                sections.append(current)
            current = None
            continue
        if current is None:
            continue
        parts = line.split()
        if len(parts) < 2:
            continue
        key = stat_key(parts[0])
        if key is None:
            continue
        try:
            current[key] = parse_value(parts[1])
        except ValueError:
            pass
    return sections


def parse_serial(serial_path):
    kernels = []
    groups = []
    pending = []
    for line in serial_path.read_text(errors="replace").splitlines():
        kernel_match = KERNEL_RE.match(line)
        if kernel_match:
            event = {
                "label": kernel_match.group(1),
                "event_ms": float(kernel_match.group(2)),
                "serial_index": len(kernels),
            }
            kernels.append(event)
            pending.append(event)
            continue
        result_match = RESULT_RE.match(line)
        if result_match:
            result = result_match.group(1)
            ok = " PASS" in f" {result} "
            groups.append(
                {
                    "result": result,
                    "ok": ok,
                    "kernel_start": (
                        pending[0]["serial_index"] if pending else None
                    ),
                    "kernel_count": len(pending),
                }
            )
            pending = []
    return kernels, groups


def usable_kernel_sections(sections):
    usable = []
    for section in sections:
        if section.get("kernels_launched", 0) <= 0:
            continue
        if section.get("completed_wgs", 0) <= 0:
            continue
        usable.append(section)
    return usable


def map_kernels(kernels, sections, skip_warmup):
    usable = usable_kernel_sections(sections)
    mapped_sections = usable[skip_warmup : skip_warmup + len(kernels)]
    mapped = []
    for kernel, section in zip(kernels, mapped_sections):
        row = dict(kernel)
        row.update(section)
        mapped.append(row)
    if len(mapped) != len(kernels):
        print(
            "warning: mapped "
            f"{len(mapped)} stats sections for {len(kernels)} serial kernels",
            file=sys.stderr,
        )
    return mapped, usable


def summarize(groups, mapped, clock_mhz):
    rows = []
    for group in groups:
        start = group["kernel_start"]
        count = group["kernel_count"]
        if start is None:
            rows.append(
                {**group, "cycles": 0, "gpu_ms": 0.0, "completed_wgs": 0}
            )
            continue
        group_kernels = mapped[start : start + count]
        cycles = sum(int(k.get("cu_cycles", 0)) for k in group_kernels)
        completed_wgs = sum(
            int(k.get("completed_wgs", 0)) for k in group_kernels
        )
        vec_ops = sum(int(k.get("vec_ops", 0)) for k in group_kernels)
        rows.append(
            {
                **group,
                "cycles": cycles,
                "gpu_ms": cycles / (clock_mhz * 1000.0),
                "completed_wgs": completed_wgs,
                "vec_ops": vec_ops,
            }
        )
    return rows


def print_summary(rows):
    print(
        "| Result | Pass | Kernels | CU cycles | GPU ms @ clock | Completed WGs | Vec ops |"
    )
    print("|---|---:|---:|---:|---:|---:|---:|")
    for row in rows:
        result = row["result"].replace("|", "\\|")
        ok = "yes" if row["ok"] else "no"
        print(
            f"| {result} | {ok} | {row['kernel_count']} | "
            f"{row['cycles']} | {row['gpu_ms']:.6f} | "
            f"{row['completed_wgs']} | {row['vec_ops']} |"
        )


def print_kernels(mapped, clock_mhz):
    print(
        "serial_index,label,section,cu_cycles,gpu_ms,completed_wgs,"
        "vec_ops,vpc,ipc,event_ms"
    )
    for row in mapped:
        cycles = int(row.get("cu_cycles", 0))
        gpu_ms = cycles / (clock_mhz * 1000.0)
        print(
            f"{row['serial_index']},{row['label']},{row.get('section', '')},"
            f"{cycles},{gpu_ms:.9f},{row.get('completed_wgs', '')},"
            f"{row.get('vec_ops', '')},{row.get('vpc', '')},"
            f"{row.get('ipc', '')},{row.get('event_ms', '')}"
        )


def main():
    parser = argparse.ArgumentParser(
        description="Summarize AMD GPUFS Rodinia per-kernel gem5 stats."
    )
    parser.add_argument("run_dir", type=Path, help="m5out run directory")
    parser.add_argument(
        "--skip-warmup",
        type=int,
        default=2,
        help="initial OpenCL/ROCm runtime GPU kernels to ignore",
    )
    parser.add_argument(
        "--clock-mhz",
        type=float,
        default=100.0,
        help="GPU shader clock in MHz for cycle-to-ms conversion",
    )
    parser.add_argument(
        "--kernels",
        action="store_true",
        help="print per-kernel CSV instead of per-result markdown",
    )
    args = parser.parse_args()

    stats_path = args.run_dir / "stats.txt"
    serial_path = args.run_dir / "system.pc.com_1.device"
    if not stats_path.is_file():
        parser.error(f"missing stats file: {stats_path}")
    if not serial_path.is_file():
        parser.error(f"missing serial log: {serial_path}")

    sections = parse_stats(stats_path)
    kernels, groups = parse_serial(serial_path)
    mapped, usable = map_kernels(kernels, sections, args.skip_warmup)

    print(
        f"run={args.run_dir} sections={len(sections)} "
        f"usable_kernel_sections={len(usable)} serial_kernels={len(kernels)} "
        f"skip_warmup={args.skip_warmup}",
        file=sys.stderr,
    )

    if args.kernels:
        print_kernels(mapped, args.clock_mhz)
    else:
        print_summary(summarize(groups, mapped, args.clock_mhz))


if __name__ == "__main__":
    main()
