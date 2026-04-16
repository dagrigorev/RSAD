#pragma once
#include "Utils.h"
#include "HypergraphMemory.h"
#include "WorldModel.h"
#include "Modes.h"
#include "Evaluator.h"
#include "ActiveSearcher.h"
#include "ProblemBank.h"
#include <vector>
#include <memory>
#include <string>

struct AgentStats {
    int    iterations=0, totalSolutions=0, validSolutions=0, finalKEff=0;
    float  bestScore=0.0f;
    double timeSeconds=0.0;
    bool   found=false;
    void print() const;
};

class RSADAgent {
public:
    RSADAgent();
    void setLLMModelPath(const std::string& path);
    void setAlphas(float a1, float a2, float a3, float a4);
    void setKMin(int k);
    void setVerbose(bool v) { verbose_ = v; }
    void initProblemBank();  ///< Инициализировать банк задач для e2
    bool isVerbose() const  { return verbose_; }
    Solution solve(const Problem& problem);
    const AgentStats& getStats()   const { return stats_; }
    HypergraphMemory& getMemory()        { return *memory_; }
    const HypergraphMemory& getMemory() const { return *memory_; }
    const Evaluator& getEvaluator() const { return *evaluator_; }
    void printBestSolution(const Problem& problem) const;
private:
    std::unique_ptr<HypergraphMemory> memory_;
    std::unique_ptr<Evaluator>        evaluator_;
    std::unique_ptr<ModeC>            modeC_;
    AgentStats   stats_;
    Solution     bestSolution_;
    std::string  llmModelPath_;
    int          kMin_    = RSADConfig::K_MIN;
    bool         verbose_ = true;
    std::shared_ptr<ProblemBank> problemBank_;

    std::vector<Solution> runAllModes(const Problem&, const WorldModel&, ModeA&, ModeB&);
    void evaluateAndFilter(std::vector<Solution>&, const std::vector<SimResult>&, const Problem&);
    void updateMemory(const Problem&, const Solution&);
};
