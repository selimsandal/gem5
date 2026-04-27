/*
 * Copyright (c) 2026 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/probes/instruction_classification_probe.hh"

#include "base/cprintf.hh"
#include "base/logging.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/limits.hh"
#include "debug/InstClassification.hh"
#include "sim/core.hh"

namespace gem5
{

InstructionClassificationProbe::ThreadCommitListener::ThreadCommitListener(
        InstructionClassificationProbe &parent, ThreadID tid)
    : ProbeListenerArgBase<o3::DynInstPtr>("Commit"),
      parent(parent),
      tid(tid)
{
}

void
InstructionClassificationProbe::ThreadCommitListener::notify(
        const o3::DynInstPtr &inst)
{
    parent.handleCommit(tid, observed, inst);
}

InstructionClassificationProbe::ClassificationStats::PerThreadStats::
PerThreadStats(statistics::Group *parent, ThreadID tid,
               statistics::Value &sim_seconds)
    : statistics::Group(parent, csprintf("thread%i", tid).c_str()),
      ADD_STAT(totalRetired, statistics::units::Count::get(),
               "Retired instructions"),
      ADD_STAT(normalRetired, statistics::units::Count::get(),
               "Retired normal instructions"),
      ADD_STAT(longLatencyRetired, statistics::units::Count::get(),
               "Retired long-latency instructions"),
      ADD_STAT(expensiveRetired, statistics::units::Count::get(),
               "Retired expensive instructions"),
      ADD_STAT(expensiveRetiredPctOfTotal, statistics::units::Ratio::get(),
               "Expensive retired instructions as a percent of all retired "
               "instructions"),
      ADD_STAT(expensiveRetiredPctOfOthers, statistics::units::Ratio::get(),
               "Expensive retired instructions as a percent of non-expensive "
               "retired instructions"),
      ADD_STAT(totalExecutionCycles, statistics::units::Cycle::get(),
               "Accumulated issue-to-complete execution cycles"),
      ADD_STAT(normalExecutionCycles, statistics::units::Cycle::get(),
               "Accumulated issue-to-complete cycles for normal instructions"),
      ADD_STAT(longLatencyExecutionCycles, statistics::units::Cycle::get(),
               "Accumulated issue-to-complete cycles for long-latency "
               "instructions"),
      ADD_STAT(expensiveExecutionCycles, statistics::units::Cycle::get(),
               "Accumulated issue-to-complete cycles for expensive "
               "instructions"),
      ADD_STAT(otherExecutionCycles, statistics::units::Cycle::get(),
               "Accumulated issue-to-complete cycles for non-expensive "
               "instructions"),
      ADD_STAT(expensiveExecutionCyclePctOfTotal,
               statistics::units::Ratio::get(),
               "Expensive issue-to-complete cycles as a percent of all "
               "issue-to-complete cycles"),
      ADD_STAT(expensiveExecutionCyclePctOfOthers,
               statistics::units::Ratio::get(),
               "Expensive issue-to-complete cycles as a percent of "
               "non-expensive issue-to-complete cycles"),
      ADD_STAT(totalEstimatedEnergy, statistics::units::Joule::get(),
               "Estimated instruction energy from configured energy-per-cycle "
               "coefficients"),
      ADD_STAT(normalEstimatedEnergy, statistics::units::Joule::get(),
               "Estimated normal-instruction energy from configured "
               "energy-per-cycle coefficients"),
      ADD_STAT(longLatencyEstimatedEnergy, statistics::units::Joule::get(),
               "Estimated long-latency-instruction energy from configured "
               "energy-per-cycle coefficients"),
      ADD_STAT(expensiveEstimatedEnergy, statistics::units::Joule::get(),
               "Estimated expensive-instruction energy from configured "
               "energy-per-cycle coefficients"),
      ADD_STAT(otherEstimatedEnergy, statistics::units::Joule::get(),
               "Estimated non-expensive-instruction energy from configured "
               "energy-per-cycle coefficients"),
      ADD_STAT(expensiveEstimatedEnergyPctOfTotal,
               statistics::units::Ratio::get(),
               "Estimated expensive-instruction energy as a percent of all "
               "estimated instruction energy"),
      ADD_STAT(expensiveEstimatedEnergyPctOfOthers,
               statistics::units::Ratio::get(),
               "Estimated expensive-instruction energy as a percent of "
               "non-expensive estimated instruction energy"),
      ADD_STAT(totalEstimatedAveragePower, statistics::units::Watt::get(),
               "Estimated average instruction power from configured "
               "energy-per-cycle coefficients"),
      ADD_STAT(normalEstimatedAveragePower, statistics::units::Watt::get(),
               "Estimated average normal-instruction power from configured "
               "energy-per-cycle coefficients"),
      ADD_STAT(longLatencyEstimatedAveragePower,
               statistics::units::Watt::get(),
               "Estimated average long-latency-instruction power from "
               "configured energy-per-cycle coefficients"),
      ADD_STAT(expensiveEstimatedAveragePower, statistics::units::Watt::get(),
               "Estimated average expensive-instruction power from configured "
               "energy-per-cycle coefficients")
{
    const statistics::Temp pct = statistics::constant(100.0);

    otherExecutionCycles = normalExecutionCycles + longLatencyExecutionCycles;
    otherEstimatedEnergy = normalEstimatedEnergy + longLatencyEstimatedEnergy;

    expensiveRetiredPctOfTotal =
        expensiveRetired / totalRetired * pct;
    expensiveRetiredPctOfOthers =
        expensiveRetired / (normalRetired + longLatencyRetired) * pct;
    expensiveExecutionCyclePctOfTotal =
        expensiveExecutionCycles / totalExecutionCycles * pct;
    expensiveExecutionCyclePctOfOthers =
        expensiveExecutionCycles / otherExecutionCycles * pct;
    expensiveEstimatedEnergyPctOfTotal =
        expensiveEstimatedEnergy / totalEstimatedEnergy * pct;
    expensiveEstimatedEnergyPctOfOthers =
        expensiveEstimatedEnergy / otherEstimatedEnergy * pct;

    totalEstimatedAveragePower = totalEstimatedEnergy / sim_seconds;
    normalEstimatedAveragePower = normalEstimatedEnergy / sim_seconds;
    longLatencyEstimatedAveragePower =
        longLatencyEstimatedEnergy / sim_seconds;
    expensiveEstimatedAveragePower =
        expensiveEstimatedEnergy / sim_seconds;

    expensiveRetiredPctOfTotal.precision(2);
    expensiveRetiredPctOfOthers.precision(2);
    expensiveExecutionCyclePctOfTotal.precision(2);
    expensiveExecutionCyclePctOfOthers.precision(2);
    expensiveEstimatedEnergyPctOfTotal.precision(2);
    expensiveEstimatedEnergyPctOfOthers.precision(2);
}

InstructionClassificationProbe::ClassificationStats::ClassificationStats(
        InstructionClassificationProbe *parent, ThreadID num_threads)
    : statistics::Group(parent),
      ADD_STAT(simSeconds, statistics::units::Second::get(),
               "Simulated seconds at stats dump")
{
    simSeconds.method(parent, &InstructionClassificationProbe::simSeconds);

    threads.reserve(num_threads);
    for (ThreadID tid = 0; tid < num_threads; ++tid)
        threads.emplace_back(
            std::make_unique<PerThreadStats>(this, tid, simSeconds));
}

InstructionClassificationProbe::ClassificationStats::PerThreadStats &
InstructionClassificationProbe::ClassificationStats::thread(ThreadID tid)
{
    return *threads[tid];
}

InstructionClassificationProbe::InstructionClassificationProbe(
        const Params &params)
    : SimObject(params),
      cpu(params.cpu),
      numThreads(cpu ? cpu->numThreads : 0),
      normalEnergyPerCycle(params.normal_energy_per_cycle),
      longLatencyEnergyPerCycle(params.long_latency_energy_per_cycle),
      expensiveEnergyPerCycle(params.expensive_energy_per_cycle),
      stats(this, numThreads)
{
    fatal_if(cpu == nullptr, "%s requires a BaseO3CPU\n", name());
    fatal_if(numThreads == 0, "%s requires at least one hardware thread\n",
             name());
    fatal_if(numThreads > o3::MaxThreads,
             "%s configured for %u threads, but O3 MaxThreads is %u\n",
             name(), static_cast<unsigned>(numThreads),
             static_cast<unsigned>(o3::MaxThreads));
}

void
InstructionClassificationProbe::regProbeListeners()
{
    SimObject::regProbeListeners();

    if (!commitListeners.empty())
        return;

    auto *manager = cpu->getProbeManager();
    for (ThreadID tid = 0; tid < numThreads; ++tid) {
        commitListeners.push_back(
            manager->connect<ThreadCommitListener>(*this, tid));
    }

    DPRINTF(InstClassification,
            "%s listening to %u hardware thread(s) on %s Commit probe\n",
            name(), static_cast<unsigned>(numThreads), cpu->name());
}

void
InstructionClassificationProbe::handleCommit(
        ThreadID tid, bool &observed, const o3::DynInstPtr &inst)
{
    if (!inst || inst->threadNumber != tid)
        return;

    if (inst->isMicroop() && !inst->isLastMicroop())
        return;

    if (!observed) {
        observed = true;
        DPRINTF(InstClassification,
                "%s observed first committed instruction for thread %u\n",
                name(), static_cast<unsigned>(tid));
    }

    auto &thread_stats = stats.thread(tid);

    const Classification classification = classify(inst);
    const uint64_t cycles = executionCycles(inst);
    const double energy = static_cast<double>(cycles) *
        energyPerCycle(classification);

    ++thread_stats.totalRetired;
    thread_stats.totalExecutionCycles += cycles;
    thread_stats.totalEstimatedEnergy += energy;

    switch (classification) {
      case Classification::Normal:
        ++thread_stats.normalRetired;
        thread_stats.normalExecutionCycles += cycles;
        thread_stats.normalEstimatedEnergy += energy;
        break;
      case Classification::LongLatency:
        ++thread_stats.longLatencyRetired;
        thread_stats.longLatencyExecutionCycles += cycles;
        thread_stats.longLatencyEstimatedEnergy += energy;
        break;
      case Classification::Expensive:
        ++thread_stats.expensiveRetired;
        thread_stats.expensiveExecutionCycles += cycles;
        thread_stats.expensiveEstimatedEnergy += energy;
        break;
    }
}

InstructionClassificationProbe::Classification
InstructionClassificationProbe::classify(const o3::DynInstPtr &inst) const
{
    (void)inst;
    /*
     * TODO: Fill in the policy. Reasonable future categories are
     * cache-missing memory ops (long latency) and integer/FP divides and
     * FP square roots (expensive).
     */
    return Classification::Normal;
}

uint64_t
InstructionClassificationProbe::executionCycles(
        const o3::DynInstPtr &inst) const
{
    if (inst->issueTick < 0 || inst->completeTick < 0 ||
        inst->completeTick < inst->issueTick) {
        return 0;
    }

    const Tick ticks = static_cast<Tick>(inst->completeTick - inst->issueTick);
    return static_cast<uint64_t>(cpu->ticksToCycles(ticks));
}

double
InstructionClassificationProbe::energyPerCycle(
        Classification classification) const
{
    switch (classification) {
      case Classification::Normal:
        return normalEnergyPerCycle;
      case Classification::LongLatency:
        return longLatencyEnergyPerCycle;
      case Classification::Expensive:
        return expensiveEnergyPerCycle;
    }

    panic("%s saw an unknown instruction classification\n", name());
}

double
InstructionClassificationProbe::simSeconds() const
{
    return static_cast<double>(curTick()) /
        static_cast<double>(sim_clock::Frequency);
}

} // namespace gem5
