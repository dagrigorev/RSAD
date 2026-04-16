#pragma once
// =============================================================================
// Trainer.h — Обучение РСАД-агента (v2: тихий режим + кривая + pruning)
// =============================================================================
#include "Utils.h"
#include "RSADAgent.h"
#include "QueryParser.h"
#include "LearningCurve.h"
#include "MemoryPruner.h"
#include <vector>
#include <string>
#include <functional>

struct CurriculumLevel {
    std::string name;
    int   gridSizeMin, gridSizeMax;
    float densityMin,  densityMax;
    int   count;
};

struct EpisodeRecord {
    int      episodeIdx;
    Problem  problem;
    Solution solution;
    bool     success;
    float    quality;
    double   timeMs;
    int      kEff;
    int      modeUsed;
};

struct TrainingStats {
    int   totalEpisodes=0, successCount=0;
    float avgQuality=0, avgKEff=0;
    float modeAWeight=1.f/3, modeBWeight=1.f/3, modeCWeight=1.f/3;
    float bestQualityEver=0;
    int   consecutiveSuccess=0;
    float successRate() const { return totalEpisodes>0?(float)successCount/totalEpisodes:0; }
    void print() const;
};

class ModeSelector {
public:
    ModeSelector();
    std::array<float,3> getWeights() const;
    void update(int modeIdx, float reward, float lr=0.05f);
    int  sample(float randVal) const;
    void print() const;
private:
    float logits_[3]={0,0,0};
    std::array<float,3> softmax() const;
};

class Trainer {
public:
    Trainer(RSADAgent& agent, int totalEpisodes=50, bool verbose=true);

    // Основной цикл
    TrainingStats run();

    // Конфигурация
    void setCurriculum(const std::vector<CurriculumLevel>& levels);
    void setDefaultCurriculum();
    void setPruneConfig(const MemoryPruner::Config& c) { pruner_ = MemoryPruner(c); }
    void setPruneInterval(int n) { pruneInterval_ = n; }

    // Callback прогресса
    using ProgressCallback = std::function<void(int,const EpisodeRecord&)>;
    void setProgressCallback(ProgressCallback cb) { callback_ = cb; }

    // Результаты
    const std::vector<EpisodeRecord>& getHistory()      const { return history_; }
    const TrainingStats&              getStats()        const { return stats_; }
    const ModeSelector&               getModeSelector() const { return modeSelector_; }
    const LearningCurve&              getCurve()        const { return curve_; }

    void saveState(const std::string& path) const;
    bool loadState(const std::string& path);

private:
    RSADAgent&                   agent_;
    int                          totalEpisodes_;
    bool                         verbose_;
    std::vector<CurriculumLevel> curriculum_;
    std::vector<EpisodeRecord>   history_;
    TrainingStats                stats_;
    ModeSelector                 modeSelector_;
    LearningCurve                curve_;
    MemoryPruner                 pruner_;
    int                          pruneInterval_ = 15;
    ProgressCallback             callback_;

    Problem sampleProblem(int ep) const;
    float   computeReward(const EpisodeRecord& r) const;
    void    updateAgent(const EpisodeRecord& r);
    void    adaptAlphas(const std::vector<EpisodeRecord>& recent);
    void    printProgress(int ep, const EpisodeRecord& r) const;
    void    printSummary() const;
};
