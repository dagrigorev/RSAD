// =============================================================================
// main_cpu.cpp v3 — РСАД: тесты, обучение, интерактив, бенчмарк
// Флаги: --test | --train [N] | --interactive | --demo | --benchmark
// =============================================================================
#include "RSADAgent.h"
#include "WorldModel.h"
#include "HypergraphMemory.h"
#include "Evaluator.h"
#include "QueryParser.h"
#include "Trainer.h"
#include "InteractiveShell.h"
#include "LearningCurve.h"
#include "SimShell.h"
#include "MemoryPruner.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <string>
#include <array>
#include <map>

#ifdef WIN32
#include <Windows.h>
#endif

// ── Benchmark ────────────────────────────────────────────────────────────────
struct BenchResult { float sr; float avgQ; double avgMs; };

BenchResult runBenchmark(RSADAgent& agent, int nProblems, unsigned seed){
    int success=0; float totalQ=0; double totalMs=0;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int>   szD(7,12);
    std::uniform_real_distribution<float>denD(0.10f,0.30f);
    bool pv=RSADGlobal::verbose; RSADGlobal::verbose=false; agent.setVerbose(false);
    for(int i=0;i<nProblems;i++){
        Problem p; int G=szD(rng); p.init(G);
        p.startX=0;p.startY=0;p.goalX=G-1;p.goalY=G-1;
        p.randomObstacles(denD(rng),(unsigned)rng());
        double t0=Utils::nowSeconds();
        Solution sol=agent.solve(p);
        double dt=Utils::nowSeconds()-t0;
        if(agent.getStats().found) success++;
        totalQ+=sol.totalScore; totalMs+=dt*1000;
    }
    agent.setVerbose(true); RSADGlobal::verbose=pv;
    return{(float)success/nProblems, totalQ/nProblems, totalMs/nProblems};
}

// ── Юнит-тесты ───────────────────────────────────────────────────────────────
namespace Tests {

void testDevice(){
    printf("[Test] Device...\n");
    int n=0; CUDA_CHECK(cudaGetDeviceCount(&n)); assert(n>0);
    cudaDeviceProp prop; CUDA_CHECK(cudaGetDeviceProperties(&prop,0));
    printf("  %s\n",prop.name);
    printf("[Test] OK\n");
}
void testSim(){
    printf("[Test] Simulation...\n");
    Problem p; p.init(5); p.startX=0;p.startY=0;p.goalX=4;p.goalY=4;
    p.randomObstacles(0.f);
    WorldModel w(p); Solution sol;
    for(int i=0;i<4;i++) sol.actions.push_back(Action::RIGHT);
    for(int i=0;i<4;i++) sol.actions.push_back(Action::DOWN);
    auto r=w.simulateCPU(sol);
    assert(r.reachedGoal&&r.stepsUsed==8);
    auto batch=w.simulateBatch({sol,sol,sol});
    assert((int)batch.size()==3);
    for(auto& rb:batch) assert(rb.reachedGoal);
    printf("[Test] OK\n");
}
void testBFS(){
    printf("[Test] BFS...\n");
    Problem p; p.init(5); p.startX=0;p.startY=0;p.goalX=4;p.goalY=4;
    p.randomObstacles(0.f);
    WorldModel w(p); auto path=w.bfsGPU(0,0,4,4);
    assert(!path.empty()&&(int)path.size()==8);
    Solution sol; sol.actions=path;
    assert(w.simulateCPU(sol).reachedGoal);
    printf("[Test] OK (len=%zu)\n",path.size());
}
void testBFSObs(){
    printf("[Test] BFS+obstacles...\n");
    Problem p; p.init(5); p.startX=0;p.startY=0;p.goalX=4;p.goalY=4;
    p.obstacles.assign(25,false);
    for(int y=0;y<4;y++) p.setObstacle(2,y,true);
    WorldModel w(p); auto path=w.bfsGPU(0,0,4,4);
    assert(!path.empty());
    Solution sol; sol.actions=path;
    assert(w.simulateCPU(sol).reachedGoal);
    printf("[Test] OK (len=%zu)\n",path.size());
}
void testMemory(){
    printf("[Test] HypergraphMemory...\n");
    HypergraphMemory m;
    auto c1=m.addConcept("a"),c2=m.addConcept("b"),c3=m.addConcept("c");
    auto e=m.addHyperedge({c1,c2,c3},RelationType::ANALOGY,1.f,"t");
    Solution s; s.actions={Action::RIGHT,Action::DOWN};
    m.attachSolution(e,s);
    assert(m.numConcepts()==3&&m.numEdges()==1);
    float q[RSADConfig::EMBED_DIM]={};
    assert(!m.findSimilarConcepts(q,3).empty());
    printf("[Test] OK\n");
}
void testEval(){
    printf("[Test] Evaluator...\n");
    Problem p; p.init(5); p.startX=0;p.startY=0;p.goalX=4;p.goalY=4;
    p.obstacles.assign(25,false);
    Solution sol;
    for(int i=0;i<4;i++) sol.actions.push_back(Action::RIGHT);
    for(int i=0;i<4;i++) sol.actions.push_back(Action::DOWN);
    SimResult r; r.reachedGoal=true;r.stepsUsed=8;r.distToGoal=0;r.finalX=4;r.finalY=4;
    std::vector<Solution> pool={sol};
    std::vector<SimResult> sims={r};
    Evaluator ev; ev.evaluate(pool,sims,p);
    printf("  e1=%.2f e2=%.2f e3=%.2f e4=%.2f total=%.2f\n",
           pool[0].e1,pool[0].e2,pool[0].e3,pool[0].e4,pool[0].totalScore);
    assert(pool[0].e1>0.5f&&pool[0].totalScore>0);
    printf("[Test] OK\n");
}
void testKEff(){
    printf("[Test] K_eff...\n");
    Solution s1,s2,s3;
    s1.actions={Action::RIGHT,Action::DOWN};
    s2.actions={Action::RIGHT,Action::DOWN};
    s3.actions={Action::DOWN,Action::RIGHT,Action::RIGHT,Action::DOWN};
    int k=Utils::computeKeff({s1,s2,s3},RSADConfig::DELTA_DIST);
    assert(k>=1); printf("[Test] OK (K=%d)\n",k);
}
void testParser(){
    printf("[Test] QueryParser...\n");
    QueryParser parser;
    struct Case{ std::string q; QueryIntent i; int gs; };
    std::vector<Case> cases={
        {"найди путь в сетке 10x10",        QueryIntent::SOLVE_PATHFINDING,10},
        {"find path grid 8 obstacles 20%",  QueryIntent::SOLVE_PATHFINDING,8},
        {"обучи агента 30 эпизодов",         QueryIntent::TRAIN,0},
        {"train 50 episodes",                QueryIntent::TRAIN,0},
        {"сложная задача",                   QueryIntent::SOLVE_PATHFINDING,12},
        {"easy maze",                        QueryIntent::SOLVE_PATHFINDING,6},
        {"hard 12x12 25%",                   QueryIntent::SOLVE_PATHFINDING,12},
        {"статистика",                       QueryIntent::SHOW_STATS,0},
        {"help",                             QueryIntent::HELP,0},
        {"реши лабиринт 15x15 от (0,0) до (14,14)",QueryIntent::SOLVE_PATHFINDING,15},
    };
    int ok=0;
    for(auto& c:cases){
        auto pr=parser.parse(c.q);
        bool pass=(pr.slots.intent==c.i);
        if(c.gs>0&&pr.slots.gridSize) pass=pass&&(*pr.slots.gridSize==c.gs);
        printf("  [%s] \"%s\"\n",pass?"OK":"!!", c.q.c_str());
        if(pass)ok++;
    }
    printf("[Test] QueryParser %d/%zu\n",ok,(size_t)cases.size());
    assert(ok>=(int)cases.size()*8/10);
    printf("[Test] OK\n");
}
void testModeSelector(){
    printf("[Test] ModeSelector...\n");
    ModeSelector sel;
    auto w0=sel.getWeights();
    for(int i=0;i<30;i++) sel.update(0,1.f,0.1f);
    auto w1=sel.getWeights();
    assert(w1[0]>w0[0]);
    printf("[Test] OK (A: %.1f%% -> %.1f%%)\n",w0[0]*100,w1[0]*100);
}
void testTraining(){
    printf("[Test] Training 5 episodes...\n");
    RSADAgent agent;
    Trainer trainer(agent,5,true);
    auto s=trainer.run();
    assert(s.totalEpisodes==5);
    printf("[Test] OK (SR=%.0f%% avgQ=%.3f)\n",s.successRate()*100,s.avgQuality);
}
void testPruner(){
    printf("[Test] MemoryPruner...\n");
    HypergraphMemory mem;
    // Добавляем 20 рёбер с разными весами
    for(int i=0;i<20;i++){
        auto c1=mem.addConcept("c"+std::to_string(i));
        auto c2=mem.addConcept("d"+std::to_string(i));
        auto eid=mem.addHyperedge({c1,c2},RelationType::SOLUTION_OF,(float)(i+1)/20.f,"e");
        Solution s; s.totalScore=(float)(i+1)/20.f;
        for(int j=0;j<i+2;j++) s.actions.push_back(Action::RIGHT);
        mem.attachSolution(eid,s);
    }
    int before=(int)mem.numEdges();
    MemoryPruner::Config cfg; cfg.maxEdges=10; cfg.minWeight=0.3f; cfg.minSolutionQuality=0.4f;
    MemoryPruner pruner(cfg);
    int removed=pruner.prune(mem);
    printf("[Test] Pruned %d/%d edges, kept %zu\n",removed,before,mem.numEdges());
    assert((int)mem.numEdges()<=10);
    printf("[Test] OK\n");
}
void testInteractive(){
    printf("[Test] InteractiveShell...\n");
    InteractiveShell shell;
    std::vector<std::string> qs={"найди путь в сетке 6x6","easy maze","find path 8x8 10%",
                                  "обучи 3 эпизода","статистика"};
    int ok=0;
    for(auto& q:qs){ auto r=shell.processQuery(q); if(r!="EXIT") ok++; }
    assert(ok==(int)qs.size());
    printf("[Test] OK (%d/%d)\n",ok,(int)qs.size());
}
void testLearningCurve(){
    printf("[Test] LearningCurve...\n");
    LearningCurve curve;
    for(int i=0;i<20;i++)
        curve.addPoint({i, (float)i/20.f, 0.3f+(float)i/40.f, 15.f, 10+i*5});
    assert((int)curve.getPoints().size()==20);
    curve.saveCSV("/tmp/test_curve.csv");
    printf("[Test] OK\n");
}

void runAll(){
    printf("\n======== РСАД Unit Tests v3 ========\n");
    testDevice(); testSim(); testBFS(); testBFSObs();
    testMemory(); testEval(); testKEff(); testParser();
    testModeSelector(); testPruner(); testLearningCurve();
    testTraining(); testInteractive();
    printf("======== Все тесты пройдены ✓ (%d) ========\n\n",13);
}
} // namespace Tests

// ── Demo ─────────────────────────────────────────────────────────────────────
void runDemo(){
    printf("\n=== Демо GridWorld 10x10 ===\n");
    Problem p; p.init(10); p.startX=0;p.startY=0;p.goalX=9;p.goalY=9;
    p.randomObstacles(0.20f,777);
    p.print();
    RSADAgent agent;
    Solution best=agent.solve(p);
    agent.getStats().print();
    if(!best.actions.empty()){ WorldModel w(p); w.printPath(best); }
}

// ── Benchmark: ДО vs ПОСЛЕ обучения ──────────────────────────────────────────
void runBenchmarkMode(int trainEpisodes){
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║       Бенчмарк: ДО / ПОСЛЕ обучения             ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");

    const int BENCH_N = 20;

    // --- ДО обучения ---
    printf("\n[1/3] Оцениваем агента ДО обучения (%d задач)...\n", BENCH_N);
    RSADAgent agent;
    BenchResult before = runBenchmark(agent, BENCH_N, 9999);
    printf("  ДО:  SR=%.1f%%  AvgQ=%.3f  AvgTime=%.1f мс  Edges=%zu\n",
           before.sr*100, before.avgQ, before.avgMs, agent.getMemory().numEdges());

    // --- Обучение ---
    printf("\n[2/3] Обучение %d эпизодов...\n", trainEpisodes);
    {
        Trainer trainer(agent, trainEpisodes, true);
        MemoryPruner::Config pc; pc.maxEdges=150; pc.minWeight=0.3f; pc.minSolutionQuality=0.45f;
        trainer.setPruneConfig(pc);
        trainer.setPruneInterval(10);
        trainer.run();
        trainer.getCurve().saveCSV("rsad_benchmark_curve.csv");
    }

    // --- ПОСЛЕ обучения ---
    printf("\n[3/3] Оцениваем агента ПОСЛЕ обучения (%d задач)...\n", BENCH_N);
    BenchResult after = runBenchmark(agent, BENCH_N, 9999);
    printf("  После: SR=%.1f%%  AvgQ=%.3f  AvgTime=%.1f мс  Edges=%zu\n",
           after.sr*100, after.avgQ, after.avgMs, agent.getMemory().numEdges());

    // --- Сравнение ---
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║                 Результаты                       ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Метрика        │    ДО    │  ПОСЛЕ   │  Дельта  ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Success Rate   │  %5.1f%%  │  %5.1f%%  │  %+.1f%%  ║\n",
           before.sr*100, after.sr*100, (after.sr-before.sr)*100);
    printf("║  Avg Quality    │  %.3f  │  %.3f  │  %+.3f  ║\n",
           before.avgQ, after.avgQ, after.avgQ-before.avgQ);
    printf("║  Avg Time (ms)  │  %6.1f  │  %6.1f  │  %+.1f   ║\n",
           before.avgMs, after.avgMs, after.avgMs-before.avgMs);
    printf("║  Memory Edges   │     --   │  %6zu  │    --    ║\n",
           agent.getMemory().numEdges());
    printf("╚══════════════════════════════════════════════════╝\n\n");

    if(after.sr > before.sr + 0.05f)
        printf("  Обучение УЛУЧШИЛО агента на %.1f%% по success rate!\n",
               (after.sr-before.sr)*100);
    else
        printf("  Агент уже работал хорошо ДО обучения (SR=%.1f%%)\n",
               before.sr*100);
}

// ── main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv){
#ifdef WIN32
    SetConsoleOutputCP(65001);
#endif

    printf("RSAD — Resonant Synthesis with Active Determination v3\n\n");
    CUDA_CHECK(cudaSetDevice(0));
    {cudaDeviceProp prop; CUDA_CHECK(cudaGetDeviceProperties(&prop,0));
     printf("Backend: %s\n\n",prop.name);}

    bool doTest=false,
        doTrain=false,
        doInteract=false,
        doDemo=false,
        doBench=false,
        doSim=false;
    int  trainN=50;

    for(int i=1;i<argc;i++){
        if(!strcmp(argv[i],"--test"))       doTest=true;
        if(!strcmp(argv[i],"--interactive"))doInteract=true;
        if(!strcmp(argv[i],"--demo"))       doDemo=true;
        if(!strcmp(argv[i],"--benchmark"))  doBench=true;
        if(!strcmp(argv[i],"--simulate")||!strcmp(argv[i],"--sim")) doSim=true;
        if(!strcmp(argv[i],"--train")){
            doTrain=true;
            if(i+1<argc&&argv[i+1][0]!='-') trainN=atoi(argv[++i]);
        }
    }
    if(!doTest&&!doTrain&&!doInteract&&!doDemo&&!doBench&&!doSim) doDemo=true;

    if(doTest)   Tests::runAll();
    if(doTrain){
        RSADAgent agent;
        Trainer trainer(agent,trainN,true);
        MemoryPruner::Config pc; pc.maxEdges=200;
        trainer.setPruneConfig(pc); trainer.setPruneInterval(15);
        trainer.run();
        trainer.saveState("rsad_state.txt");
        trainer.getCurve().saveCSV("rsad_learning_curve.csv");
    }
    if(doBench)  runBenchmarkMode(trainN>0?trainN:40);
    if(doDemo)   runDemo();
    if(doInteract){InteractiveShell shell; shell.run();}
    if(doSim){SimShell sim; sim.run();}

    CUDA_CHECK(cudaDeviceReset());
    return 0;
}
