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
PerThreadStats(statistics::Group *parent, ThreadID tid)
    : statistics::Group(parent, csprintf("thread%i", tid).c_str()),
      ADD_STAT(totalRetired, statistics::units::Count::get(),
               "Retired instructions"),
      ADD_STAT(longLatencyRetired, statistics::units::Count::get(),
               "Retired long-latency instructions"),
      ADD_STAT(expensiveRetired, statistics::units::Count::get(),
               "Retired expensive instructions")
{
}

InstructionClassificationProbe::ClassificationStats::ClassificationStats(
        InstructionClassificationProbe *parent, ThreadID num_threads)
    : statistics::Group(parent)
{
    threads.reserve(num_threads);
    for (ThreadID tid = 0; tid < num_threads; ++tid)
        threads.emplace_back(std::make_unique<PerThreadStats>(this, tid));
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

    ++thread_stats.totalRetired;

    switch (classify(inst)) {
      case Classification::Normal:
        break;
      case Classification::LongLatency:
        ++thread_stats.longLatencyRetired;
        break;
      case Classification::Expensive:
        ++thread_stats.expensiveRetired;
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

} // namespace gem5
