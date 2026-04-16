// needs map
// =============================================================================
// Trainer.cpp — Реализация обучающего цикла v2
// =============================================================================
#include "Trainer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <fstream>
#include <sstream>
#include <map>
#include <array>

// ── ModeSelector ─────────────────────────────────────────────────────────────
ModeSelector::ModeSelector() { logits_[0]=0.f; logits_[1]=0.3f; logits_[2]=-0.1f; }

std::array<float,3> ModeSelector::softmax() const {
    float mx=*std::max_element(logits_,logits_+3);
    float e[3]; float s=0;
    for(int i=0;i<3;i++){e[i]=expf(logits_[i]-mx);s+=e[i];}
    return { e[0]/s,e[1]/s,e[2]/s };
}
std::array<float,3> ModeSelector::getWeights() const{return softmax();}

void ModeSelector::update(int idx,float reward,float lr){
    if(idx<0||idx>=3)return;
    auto p=softmax();
    for(int i=0;i<3;i++){
        float ind=(i==idx)?1.f:0.f;
        logits_[i]+=lr*reward*(ind-p[i]);
        logits_[i]=std::max(-5.f,std::min(5.f,logits_[i]));
    }
}
int ModeSelector::sample(float r) const{
    auto p=softmax(); float c=0;
    for(int i=0;i<3;i++){c+=p[i];if(r<=c)return i;}
    return 2;
}
void ModeSelector::print() const{
    auto w=softmax();
    printf("  Mode weights: A=%.1f%%  B=%.1f%%  C=%.1f%%\n",w[0]*100,w[1]*100,w[2]*100);
}

// ── TrainingStats ─────────────────────────────────────────────────────────────
void TrainingStats::print() const{
    printf("\n╔══════════════════════════════════════╗\n");
    printf("║      Статистика обучения РСАД        ║\n");
    printf("╠══════════════════════════════════════╣\n");
    printf("║  Эпизодов:     %-5d                 ║\n",totalEpisodes);
    printf("║  Успешных:     %-5d  (%.0f%%)          ║\n",successCount,successRate()*100);
    printf("║  Ср. качество: %.3f                ║\n",avgQuality);
    printf("║  Ср. K_eff:    %.1f                 ║\n",avgKEff);
    printf("║  Лучшее:       %.3f                ║\n",bestQualityEver);
    printf("║  Веса режимов: A=%.0f%% B=%.0f%% C=%.0f%%  ║\n",
           modeAWeight*100,modeBWeight*100,modeCWeight*100);
    printf("╚══════════════════════════════════════╝\n");
}

// ── Trainer ───────────────────────────────────────────────────────────────────
Trainer::Trainer(RSADAgent& agent, int n, bool verbose)
    : agent_(agent), totalEpisodes_(n), verbose_(verbose)
{ setDefaultCurriculum(); }

void Trainer::setDefaultCurriculum(){
    curriculum_={
        {"easy",   4, 6,  0.00f,0.10f,10},
        {"medium", 7,10,  0.10f,0.20f,15},
        {"hard",  10,14,  0.20f,0.30f,15},
        {"extreme",8,12,  0.28f,0.36f,10},
    };
}
void Trainer::setCurriculum(const std::vector<CurriculumLevel>& lv){curriculum_=lv;}

Problem Trainer::sampleProblem(int ep) const{
    int tot=0; for(auto& l:curriculum_) tot+=l.count;
    int sc=(ep*tot)/std::max(1,totalEpisodes_);
    int cum=0;
    const CurriculumLevel* lev=&curriculum_.back();
    for(auto& l:curriculum_){cum+=l.count;if(sc<cum){lev=&l;break;}}
    std::mt19937 rng(ep*31337+42);
    std::uniform_int_distribution<int>  szD(lev->gridSizeMin,lev->gridSizeMax);
    std::uniform_real_distribution<float>denD(lev->densityMin,lev->densityMax);
    std::uniform_int_distribution<unsigned>sdD(0,999999);
    Problem p; int G=szD(rng); p.init(G);
    p.startX=0;p.startY=0;p.goalX=G-1;p.goalY=G-1;
    p.randomObstacles(denD(rng),sdD(rng));
    return p;
}

float Trainer::computeReward(const EpisodeRecord& r) const{
    float rw=r.quality;
    if(r.success){
        int opt=abs(r.problem.goalX-r.problem.startX)+abs(r.problem.goalY-r.problem.startY);
        int act=(int)r.solution.actions.size();
        if(act>0) rw+=0.2f*(float)opt/act;
    }
    return std::min(1.5f,std::max(-0.5f,rw));
}

void Trainer::updateAgent(const EpisodeRecord& r){
    float rw=computeReward(r);
    modeSelector_.update(r.modeUsed,rw,0.05f);
    stats_.totalEpisodes++;
    if(r.success){stats_.successCount++;stats_.consecutiveSuccess++;}
    else stats_.consecutiveSuccess=0;
    float a=0.1f;
    stats_.avgQuality=(1-a)*stats_.avgQuality+a*r.quality;
    stats_.avgKEff   =(1-a)*stats_.avgKEff   +a*(float)r.kEff;
    stats_.bestQualityEver=std::max(stats_.bestQualityEver,r.quality);
    auto w=modeSelector_.getWeights();
    stats_.modeAWeight=w[0];stats_.modeBWeight=w[1];stats_.modeCWeight=w[2];
}

void Trainer::adaptAlphas(const std::vector<EpisodeRecord>& rec){
    if(rec.size()<5) return;
    float sr=0; for(auto& r:rec) if(r.success) sr+=1.f;
    sr/=rec.size();
    if     (sr<0.30f) agent_.setAlphas(0.50f,0.25f,0.10f,0.15f);
    else if(sr>0.80f) agent_.setAlphas(0.35f,0.20f,0.30f,0.15f);
    else              agent_.setAlphas(RSADConfig::ALPHA1,RSADConfig::ALPHA2,
                                       RSADConfig::ALPHA3,RSADConfig::ALPHA4);
}

void Trainer::printProgress(int ep, const EpisodeRecord& r) const{
    // Прогресс-бар с текущим SR
    int barW=20;
    int filled=(int)((float)(ep+1)/totalEpisodes_*barW);
    char bar[22]; for(int i=0;i<barW;i++) bar[i]=(i<filled)?'#':'-'; bar[barW]=0;
    printf("\r[%s] %3d/%d %s Q=%.3f SR=%3.0f%% K=%2d  ",
           bar, ep+1, totalEpisodes_,
           r.success?"OK":"--", r.quality,
           stats_.successRate()*100, r.kEff);
    fflush(stdout);
}

void Trainer::printSummary() const{
    printf("\n\n");
    stats_.print();
    printf("\nУспешность по размеру сетки:\n");
    std::map<int,std::pair<int,int>> bySz;
    for(auto& r:history_){bySz[r.problem.gridSize].second++;if(r.success)bySz[r.problem.gridSize].first++;}
    for(auto& [sz,cnt]:bySz){
        float sr=cnt.second>0?(float)cnt.first/cnt.second*100:0;
        int b=(int)(sr/5); char prog[21]; for(int i=0;i<20;i++) prog[i]=(i<b)?'#':'-'; prog[20]=0;
        printf("  %2dx%2d [%s] %2d/%2d (%.0f%%)\n",sz,sz,prog,cnt.first,cnt.second,sr);
    }
    printf("\n");
    modeSelector_.print();
    curve_.printTable(std::max(1,totalEpisodes_/10));
    curve_.print(50,10);
    curve_.saveCSV("rsad_learning_curve.csv");
}

TrainingStats Trainer::run(){
    printf("\n");
    printf("╔══════════════════════════════════════════════╗\n");
    printf("║         Обучение РСАД-агента v2              ║\n");
    printf("║  Эпизодов: %-5d  Уровней: %-3zu  Pruning:%d  ║\n",
           totalEpisodes_,(size_t)curriculum_.size(),pruneInterval_);
    printf("╚══════════════════════════════════════════════╝\n\n");

    // Переводим агент в тихий режим
    agent_.setVerbose(false);
    bool prevGlobal = RSADGlobal::verbose;
    RSADGlobal::verbose = false;

    history_.reserve(totalEpisodes_);
    std::mt19937 rng(777);
    std::uniform_real_distribution<float> uD(0.f,1.f);

    for(int ep=0;ep<totalEpisodes_;ep++){
        double t0=Utils::nowSeconds();
        Problem prob=sampleProblem(ep);
        Solution sol=agent_.solve(prob);
        const AgentStats& as=agent_.getStats();

        // Определяем mode: если BFS нашёл оптимальный → B; иначе A или C
        int modeUsed=1;
        if(sol.e3>0.65f) modeUsed=2;   // элегантное → C (метафора)
        else if(!as.found) modeUsed=0;  // провал → A (аналогия)

        EpisodeRecord rec;
        rec.episodeIdx=ep; rec.problem=prob; rec.solution=sol;
        rec.success=as.found; rec.quality=sol.totalScore;
        rec.timeMs=(Utils::nowSeconds()-t0)*1000.0;
        rec.kEff=as.finalKEff; rec.modeUsed=modeUsed;

        updateAgent(rec);
        history_.push_back(rec);

        // Точка кривой обучения
        curve_.addPoint({ep, stats_.successRate(), stats_.avgQuality,
                         stats_.avgKEff, (int)agent_.getMemory().numEdges()});

        // Адаптация alphas
        if(ep>0 && ep%10==0){
            int w=std::min(10,ep);
            adaptAlphas(std::vector<EpisodeRecord>(history_.end()-w,history_.end()));
        }

        // Pruning памяти
        if(ep>0 && ep%pruneInterval_==0)
            pruner_.prune(agent_.getMemory());

        if(verbose_) printProgress(ep,rec);
        if(callback_) callback_(ep,rec);
    }

    // Восстанавливаем режим
    agent_.setVerbose(true);
    RSADGlobal::verbose = prevGlobal;

    printSummary();
    return stats_;
}

void Trainer::saveState(const std::string& path) const{
    std::ofstream f(path);
    if(!f){fprintf(stderr,"[Trainer] Cannot save: %s\n",path.c_str());return;}
    f<<"episodes="<<stats_.totalEpisodes<<"\n"
     <<"success="<<stats_.successCount<<"\n"
     <<"avg_quality="<<stats_.avgQuality<<"\n"
     <<"avg_keff="<<stats_.avgKEff<<"\n"
     <<"best_quality="<<stats_.bestQualityEver<<"\n";
    auto w=modeSelector_.getWeights();
    f<<"mode_a="<<w[0]<<"\n"<<"mode_b="<<w[1]<<"\n"<<"mode_c="<<w[2]<<"\n";
    printf("[Trainer] State saved to %s\n",path.c_str());
}

bool Trainer::loadState(const std::string& path){
    std::ifstream f(path); if(!f) return false;
    std::string line;
    while(std::getline(f,line)){
        auto sep=line.find('='); if(sep==std::string::npos) continue;
        std::string k=line.substr(0,sep),v=line.substr(sep+1);
        if(k=="episodes")    stats_.totalEpisodes=std::stoi(v);
        if(k=="success")     stats_.successCount=std::stoi(v);
        if(k=="avg_quality") stats_.avgQuality=std::stof(v);
        if(k=="avg_keff")    stats_.avgKEff=std::stof(v);
        if(k=="best_quality")stats_.bestQualityEver=std::stof(v);
    }
    printf("[Trainer] State loaded from %s\n",path.c_str());
    return true;
}
