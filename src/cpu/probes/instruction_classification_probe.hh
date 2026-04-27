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

#ifndef __CPU_PROBES_INSTRUCTION_CLASSIFICATION_PROBE_HH__
#define __CPU_PROBES_INSTRUCTION_CLASSIFICATION_PROBE_HH__

#include <memory>
#include <vector>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/o3/cpu.hh"
#include "cpu/o3/dyn_inst_ptr.hh"
#include "params/InstructionClassificationProbe.hh"
#include "sim/probe/probe.hh"
#include "sim/sim_object.hh"

namespace gem5
{

class InstructionClassificationProbe : public SimObject
{
  public:
    PARAMS(InstructionClassificationProbe);

    explicit InstructionClassificationProbe(const Params &params);

    void regProbeListeners() override;

  private:
    enum class Classification
    {
        Normal,
        LongLatency,
        Expensive,
    };

    class ThreadCommitListener : public ProbeListenerArgBase<o3::DynInstPtr>
    {
      public:
        ThreadCommitListener(InstructionClassificationProbe &parent,
                             ThreadID tid);

        void notify(const o3::DynInstPtr &inst) override;

      private:
        InstructionClassificationProbe &parent;
        const ThreadID tid;
        bool observed = false;
    };

    struct ClassificationStats : public statistics::Group
    {
        struct PerThreadStats : public statistics::Group
        {
            PerThreadStats(statistics::Group *parent, ThreadID tid,
                           statistics::Value &sim_seconds);

            statistics::Scalar totalRetired;
            statistics::Scalar normalRetired;
            statistics::Scalar longLatencyRetired;
            statistics::Scalar expensiveRetired;
            statistics::Formula expensiveRetiredPctOfTotal;
            statistics::Formula expensiveRetiredPctOfOthers;

            statistics::Scalar totalExecutionCycles;
            statistics::Scalar normalExecutionCycles;
            statistics::Scalar longLatencyExecutionCycles;
            statistics::Scalar expensiveExecutionCycles;
            statistics::Formula otherExecutionCycles;
            statistics::Formula expensiveExecutionCyclePctOfTotal;
            statistics::Formula expensiveExecutionCyclePctOfOthers;

            statistics::Scalar totalEstimatedEnergy;
            statistics::Scalar normalEstimatedEnergy;
            statistics::Scalar longLatencyEstimatedEnergy;
            statistics::Scalar expensiveEstimatedEnergy;
            statistics::Formula otherEstimatedEnergy;
            statistics::Formula expensiveEstimatedEnergyPctOfTotal;
            statistics::Formula expensiveEstimatedEnergyPctOfOthers;
            statistics::Formula totalEstimatedAveragePower;
            statistics::Formula normalEstimatedAveragePower;
            statistics::Formula longLatencyEstimatedAveragePower;
            statistics::Formula expensiveEstimatedAveragePower;
        };

        ClassificationStats(InstructionClassificationProbe *parent,
                            ThreadID num_threads);

        PerThreadStats &thread(ThreadID tid);

      private:
        statistics::Value simSeconds;
        std::vector<std::unique_ptr<PerThreadStats>> threads;
    };

    o3::CPU *const cpu;
    const ThreadID numThreads;
    const double normalEnergyPerCycle;
    const double longLatencyEnergyPerCycle;
    const double expensiveEnergyPerCycle;
    std::vector<ProbeListenerPtr<ThreadCommitListener>> commitListeners;
    ClassificationStats stats;

    void handleCommit(ThreadID tid, bool &observed,
                      const o3::DynInstPtr &inst);

    Classification classify(const o3::DynInstPtr &inst) const;
    uint64_t executionCycles(const o3::DynInstPtr &inst) const;
    double energyPerCycle(Classification classification) const;
    double simSeconds() const;
};

} // namespace gem5

#endif // __CPU_PROBES_INSTRUCTION_CLASSIFICATION_PROBE_HH__
