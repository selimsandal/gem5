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

"""Run RISC-V Geekbench in SE mode with instruction-classification stats.

Example:

```
build/RISCV/gem5.opt -d ~/Developer/geek_results \
    configs/example/riscv_geekbench_probe.py \
    --interp-dir /path/to/riscv/sysroot \
    --chroot /path/to/riscv/sysroot
```

The Geekbench preview binaries are dynamically linked, so a RISC-V sysroot
containing `/lib/ld-linux-riscv64-lp64d.so.1` is required on non-RISC-V hosts.
On native RISC-V hosts, the host loader and libraries can be used directly.

Install the minimal Ubuntu runtime sysroot with:

```
sudo apt install --no-install-recommends libc6-riscv64-cross libgcc-s1-riscv64-cross
```
"""

import argparse
import os
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


DEFAULT_GEEKBENCH_DIR = (
    "/home/selimsandal/Developer/Geekbench-6.7.0-LinuxRISCVPreview"
)


def build_process(args):
    binary = args.cmd
    if not binary:
        binary = os.path.join(args.geekbench_dir, args.geekbench_binary)

    binary = os.path.abspath(os.path.expanduser(binary))
    geekbench_dir = os.path.abspath(os.path.expanduser(args.geekbench_dir))

    process = Process(pid=100)
    process.executable = binary
    process.cwd = geekbench_dir
    process.gid = os.getgid()

    if args.env:
        with open(args.env) as f:
            process.env = [line.rstrip() for line in f]

    workload_args = args.options or args.geekbench_args
    process.cmd = [binary] + workload_args.split()

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
    "--geekbench-dir",
    default=DEFAULT_GEEKBENCH_DIR,
    help="Directory containing the Geekbench RISC-V preview files.",
)
parser.add_argument(
    "--geekbench-binary",
    default="geekbench6",
    choices=["geekbench6", "geekbench_riscv64", "geekbench_rv64gcv"],
    help="Geekbench binary to execute when --cmd is not set.",
)
parser.add_argument(
    "--geekbench-args",
    default="",
    help="Arguments passed to Geekbench when --options is not set.",
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

args = parser.parse_args()

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
