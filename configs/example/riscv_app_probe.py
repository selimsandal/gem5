# Copyright (c) 2026 The Regents of the University of California
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""Run a RISC-V SE-mode app with instruction-classification stats.

Example:

```
build/RISCV/gem5.opt configs/example/riscv_app_probe.py \
    --cmd /path/to/riscv/app \
    --options "app arguments"
```

If `--cmd` is omitted, this config runs the local Geekbench preview by
default:

```
build/RISCV/gem5.opt configs/example/riscv_app_probe.py
```

Some RISC-V applications are dynamically linked. On non-RISC-V hosts, those
apps use `--riscv-sysroot`, which defaults to `/usr/riscv64-linux-gnu`. On
native RISC-V hosts, the host loader and libraries are used directly.

Install the minimal Ubuntu runtime sysroot with:

```
sudo apt install --no-install-recommends libc6-riscv64-cross libgcc-s1-riscv64-cross
```
"""

import argparse
import os
import platform
import shlex
import subprocess
import sys

import m5
from m5.objects import *
from m5.util import (
    addToPath,
    fatal,
)

addToPath("../")

from common import (  # noqa: E402
    CacheConfig,
    MemConfig,
    ObjectList,
    Options,
    Simulation,
)
from common.FileSystemConfig import config_filesystem  # noqa: E402
from ruby import Ruby  # noqa: E402


DEFAULT_APP_DIR = "~/Developer/Geekbench-6.7.0-LinuxRISCVPreview"
DEFAULT_APP_BINARY = "geekbench6"
DEFAULT_RISCV_SYSROOT = "/usr/riscv64-linux-gnu"
RISCV_DYNAMIC_LOADER = "lib/ld-linux-riscv64-lp64d.so.1"


def expand_path(path):
    return os.path.abspath(os.path.expanduser(path))


def is_native_riscv_host():
    return platform.machine().startswith("riscv64")


def binary_has_interpreter(binary):
    try:
        result = subprocess.run(
            ["readelf", "-l", binary],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except FileNotFoundError:
        return None

    return "Requesting program interpreter" in result.stdout


def normalize_app_args(args):
    args.app_dir = args.app_dir or args.geekbench_dir or DEFAULT_APP_DIR
    args.app_binary = (
        args.app_binary or args.geekbench_binary or DEFAULT_APP_BINARY
    )

    if args.app_args is None:
        args.app_args = args.geekbench_args or ""


def resolve_binary(args):
    if args.cmd:
        return expand_path(args.cmd)

    app_binary = os.path.expanduser(args.app_binary)
    if os.path.isabs(app_binary):
        return expand_path(app_binary)

    return expand_path(os.path.join(args.app_dir, app_binary))


def resolve_cwd(args, binary):
    if args.app_cwd:
        return expand_path(args.app_cwd)

    app_binary = os.path.expanduser(args.app_binary)
    if args.cmd or os.path.isabs(app_binary):
        return os.path.dirname(binary)

    return expand_path(args.app_dir)


def wants_riscv_sysroot(args, binary):
    use_sysroot = args.use_riscv_sysroot.lower()
    if use_sysroot in ("1", "true", "yes"):
        return True
    if use_sysroot in ("0", "false", "no"):
        return False
    if use_sysroot != "auto":
        fatal(
            "Invalid --use-riscv-sysroot=%s. Use auto, 1, or 0.",
            args.use_riscv_sysroot,
        )

    if is_native_riscv_host():
        return False

    has_interpreter = binary_has_interpreter(binary)
    if has_interpreter is False:
        return False

    return True


def configure_runtime_paths(args, binary):
    if not wants_riscv_sysroot(args, binary):
        return

    sysroot = expand_path(args.riscv_sysroot)
    loader = os.path.join(sysroot, RISCV_DYNAMIC_LOADER)
    if not os.path.exists(loader):
        fatal(
            "RISC-V dynamic loader not found: %s. Install the runtime "
            "sysroot or pass --use-riscv-sysroot=0 for a static binary.",
            loader,
        )

    if args.interp_dir is None:
        args.interp_dir = sysroot

    lib_redirect = f"/lib={os.path.join(sysroot, 'lib')}"
    has_lib_redirect = any(
        redirect.startswith("/lib=") for redirect in args.redirects
    )
    if not has_lib_redirect:
        args.redirects.append(lib_redirect)


def build_process(args):
    binary = resolve_binary(args)
    cwd = resolve_cwd(args, binary)

    if not os.path.exists(binary):
        fatal("Application binary not found: %s", binary)

    process = Process(pid=100)
    process.executable = binary
    process.cwd = cwd
    process.gid = os.getgid()

    if args.env:
        with open(args.env) as f:
            process.env = [line.rstrip() for line in f]

    workload_args = args.options or args.app_args
    process.cmd = [binary] + shlex.split(workload_args)

    if args.input:
        process.input = args.input
    if args.output:
        process.output = args.output
    if args.errout:
        process.errout = args.errout

    return process


parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)

if "--ruby" in sys.argv:
    Ruby.define_options(parser)

parser.set_defaults(
    cpu_type="RiscvO3CPU",
    caches=True,
    l2cache=True,
    mem_size="4GiB",
)

parser.add_argument(
    "--app-dir",
    default=None,
    help=(
        "Directory containing the default RISC-V application. Used when "
        "--cmd is not set."
    ),
)
parser.add_argument(
    "--app-binary",
    default=None,
    help=(
        "RISC-V application binary to execute when --cmd is not set. "
        "Relative paths are resolved under --app-dir."
    ),
)
parser.add_argument(
    "--app-args",
    default=None,
    help="Arguments passed to --app-binary when --options is not set.",
)
parser.add_argument(
    "--app-cwd",
    default=None,
    help=(
        "Process working directory. Defaults to --app-dir, or to the "
        "binary directory when --cmd is set."
    ),
)
parser.add_argument(
    "--riscv-sysroot",
    default=DEFAULT_RISCV_SYSROOT,
    help=(
        "RISC-V runtime sysroot used for dynamically linked apps on "
        "non-RISC-V hosts."
    ),
)
parser.add_argument(
    "--use-riscv-sysroot",
    default="auto",
    help=(
        "Use --riscv-sysroot defaults: auto, 1, or 0. Auto skips the "
        "sysroot on native RISC-V hosts and for static binaries."
    ),
)
parser.add_argument(
    "--geekbench-dir",
    default=None,
    help="Legacy alias for --app-dir.",
)
parser.add_argument(
    "--geekbench-binary",
    default=None,
    help="Legacy alias for --app-binary.",
)
parser.add_argument(
    "--geekbench-args",
    default=None,
    help="Legacy alias for --app-args.",
)
parser.add_argument(
    "--normal-energy-per-cycle",
    default="0pJ",
    help="Probe energy coefficient for normal instructions.",
)
parser.add_argument(
    "--long-latency-energy-per-cycle",
    default="0pJ",
    help="Probe energy coefficient for long-latency instructions.",
)
parser.add_argument(
    "--expensive-energy-per-cycle",
    default="0pJ",
    help="Probe energy coefficient for expensive instructions.",
)
parser.add_argument(
    "--long-latency-min-cycles",
    default=1,
    type=int,
    help=(
        "Minimum issue-to-complete cycles for a memory instruction to be "
        "classified as long-latency."
    ),
)

args = parser.parse_args()
normalize_app_args(args)
configure_runtime_paths(args, resolve_binary(args))

CPUClass, test_mem_mode, FutureClass = Simulation.setCPUClass(args)
CPUClass.numThreads = 1

if not ObjectList.is_o3_cpu(CPUClass):
    fatal("This config is intended for an O3 CPU.")

if not args.caches:
    fatal("RiscvO3CPU requires caches for this config.")

process = build_process(args)

system = System(
    cpu=[CPUClass(cpu_id=i) for i in range(args.num_cpus)],
    mem_mode=test_mem_mode,
    mem_ranges=[AddrRange(args.mem_size)],
    cache_line_size=args.cacheline_size,
)

system.voltage_domain = VoltageDomain(voltage=args.sys_voltage)
system.clk_domain = SrcClockDomain(
    clock=args.sys_clock,
    voltage_domain=system.voltage_domain,
)
system.cpu_voltage_domain = VoltageDomain()
system.cpu_clk_domain = SrcClockDomain(
    clock=args.cpu_clock,
    voltage_domain=system.cpu_voltage_domain,
)

for cpu in system.cpu:
    cpu.clk_domain = system.cpu_clk_domain
    cpu.workload = process
    cpu.createThreads()
    cpu.inst_classification = InstructionClassificationProbe(
        cpu=cpu,
        normal_energy_per_cycle=args.normal_energy_per_cycle,
        long_latency_energy_per_cycle=args.long_latency_energy_per_cycle,
        expensive_energy_per_cycle=args.expensive_energy_per_cycle,
        long_latency_min_cycles=args.long_latency_min_cycles,
    )

if args.ruby:
    Ruby.create_system(args, False, system)
    assert args.num_cpus == len(system.ruby._cpu_ports)
    system.ruby.clk_domain = SrcClockDomain(
        clock=args.ruby_clock,
        voltage_domain=system.voltage_domain,
    )

    for i, ruby_port in enumerate(system.ruby._cpu_ports):
        system.cpu[i].createInterruptController()
        ruby_port.connectCpuPorts(system.cpu[i])
else:
    system.membus = SystemXBar()
    system.system_port = system.membus.cpu_side_ports
    CacheConfig.config_cache(args, system)
    MemConfig.config_mem(args, system)
    config_filesystem(system, args)

system.workload = SEWorkload.init_compatible(process.executable)

if args.wait_gdb:
    system.workload.wait_for_remote_gdb = True

root = Root(full_system=False, system=system)
Simulation.run(args, root, system, FutureClass)
