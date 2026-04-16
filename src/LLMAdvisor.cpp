// =============================================================================
// LLMAdvisor.cpp — Реализация LLM-советника
// =============================================================================
#include "LLMAdvisor.h"
#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <queue>
#include <random>
#include <numeric>

LLMAdvisor::LLMAdvisor(void*, const WorldModel* world) : world_(world) { initPrompts(); }

// ---------------------------------------------------------------------------
// Инициализация алгоритмических "промптов"
// ---------------------------------------------------------------------------
void LLMAdvisor::initPrompts() {
    // Промпты — это именованные стратегии поиска
    // Их можно передать реальному LLM как инструкции: "решай как X"
    prompts_.push_back({"spiral",    "Обходи сетку по спирали от угла к центру", {}});
    prompts_.push_back({"zigzag_v",  "Иди вертикальными полосами, чередуя вверх/вниз", {}});
    prompts_.push_back({"zigzag_h",  "Иди горизонтальными полосами", {}});
    prompts_.push_back({"astar_w1",  "A* с весом препятствий 1.0", {}});
    prompts_.push_back({"astar_w2",  "A* с весом препятствий 2.0 — обходи широко", {}});
    prompts_.push_back({"quad",      "Разбей задачу на квадранты и реши каждый", {}});
    prompts_.push_back({"failure",   "Обходи клетки с историей отказов", {}});
}

// ---------------------------------------------------------------------------
// Карта неудач: анализируем пул — где решения чаще всего ломаются
// ---------------------------------------------------------------------------
FailureMap LLMAdvisor::buildFailureMap(
    const std::vector<Solution>& pool, const Problem& problem) const
{
    int W = problem.gridSize, H = problem.gridSize;
    FailureMap fm;
    fm.width  = W; fm.height = H;
    fm.heatmap.assign(W*H, 0.0f);
    fm.worstX = W/2; fm.worstY = H/2;

    if (pool.empty()) return fm;

    // Для каждого решения в пуле симулируем его и записываем
    // позицию остановки (= место неудачи) в карту
    for (auto& sol : pool) {
        // Быстрая CPU симуляция
        int x = problem.startX, y = problem.startY;
        bool failed = false;

        for (auto a : sol.actions) {
            int nx=x, ny=y;
            switch(a) {
                case Action::UP:    ny--; break;
                case Action::DOWN:  ny++; break;
                case Action::LEFT:  nx--; break;
                case Action::RIGHT: nx++; break;
                default: break;
            }
            if (problem.isObstacle(nx,ny)) {
                // Отказ: клетка перед препятствием — проблемная
                if (x>=0&&x<W&&y>=0&&y<H)
                    fm.heatmap[y*W+x] += 1.0f;
                failed = true; break;
            }
            x=nx; y=ny;
        }

        // Если решение хорошее (e1 > 0.7), помечаем посещённые клетки как "безопасные"
        // (отрицательный heat — снижает тепло вокруг)
        if (!failed && sol.e1 > 0.7f) {
            int x2=problem.startX, y2=problem.startY;
            for (auto a : sol.actions) {
                if (x2>=0&&x2<W&&y2>=0&&y2<H)
                    fm.heatmap[y2*W+x2] -= 0.3f;
                switch(a) {
                    case Action::UP:    y2--; break;
                    case Action::DOWN:  y2++; break;
                    case Action::LEFT:  x2--; break;
                    case Action::RIGHT: x2++; break;
                    default: break;
                }
            }
        }
    }

    // Нормализуем
    float maxH = 0;
    for (float v : fm.heatmap) maxH = std::max(maxH, v);
    if (maxH > 0)
        for (float& v : fm.heatmap) v = std::max(0.0f, v/maxH);

    // Находим самую горячую свободную клетку
    float bestHeat = -1;
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        if (!problem.isObstacle(x,y) && fm.heatmap[y*W+x] > bestHeat) {
            bestHeat = fm.heatmap[y*W+x];
            fm.worstX = x; fm.worstY = y;
        }
    }

    return fm;
}

// ---------------------------------------------------------------------------
// Описание состояния пула (для промпта к LLM)
// ---------------------------------------------------------------------------
std::string LLMAdvisor::describePool(
    const std::vector<Solution>& pool, const Problem& problem) const
{
    if (pool.empty()) return "пул пуст";

    int reached=0; float avgLen=0; float avgScore=0;
    for (auto& s : pool) {
        if (s.e1 > 0.7f) reached++;
        avgLen   += s.actions.size();
        avgScore += s.totalScore;
    }
    avgLen   /= pool.size();
    avgScore /= pool.size();

    std::ostringstream ss;
    ss << "Сетка " << problem.gridSize << "×" << problem.gridSize
       << " (" << problem.startX << "," << problem.startY << ")→"
       << "(" << problem.goalX << "," << problem.goalY << "). "
       << "Пул: " << pool.size() << " решений, "
       << reached << " достигают цели, "
       << "средняя длина=" << (int)avgLen << ", "
       << "средний score=" << avgScore << ". "
       << "Мин. Манхэттен: "
       << (abs(problem.goalX-problem.startX)+abs(problem.goalY-problem.startY))
       << ". Генерируй оптимальный путь как последовательность U/D/L/R.";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Генерация пути в обход проблемных регионов
// ---------------------------------------------------------------------------
Solution LLMAdvisor::generateFailureAware(
    const Problem& problem, const FailureMap& fm, unsigned seed) const
{
    // Модифицированный A*: штрафуем горячие клетки
    int G = problem.gridSize, GG = G*G;
    std::vector<float> cost(GG, 1e9f);
    std::vector<int>   parent(GG, -1);
    std::vector<Action>parentAct(GG, Action::NONE);

    int startFlat = problem.startY*G + problem.startX;
    int goalFlat  = problem.goalY*G  + problem.goalX;
    cost[startFlat] = 0.0f;

    // Приоритетная очередь: (f, flat)
    using QEl = std::pair<float,int>;
    std::priority_queue<QEl, std::vector<QEl>, std::greater<QEl>> pq;
    pq.push({0.0f, startFlat});

    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> noise(-0.1f, 0.1f);

    auto heuristic = [&](int flat) {
        int x=flat%G, y=flat/G;
        return (float)(abs(x-problem.goalX)+abs(y-problem.goalY));
    };

    while (!pq.empty()) {
        auto [f, flat] = pq.top(); pq.pop();
        if (flat == goalFlat) break;
        if (f > cost[flat]+0.5f) continue; // устаревший

        int cx=flat%G, cy=flat/G;
        const int DX[]={0,0,-1,1}, DY[]={-1,1,0,0};
        const Action ACTS[]={Action::UP,Action::DOWN,Action::LEFT,Action::RIGHT};

        for (int d=0;d<4;d++) {
            int nx=cx+DX[d], ny=cy+DY[d];
            if (nx<0||nx>=G||ny<0||ny>=G) continue;
            if (problem.isObstacle(nx,ny)) continue;
            int nflat=ny*G+nx;

            // Стоимость хода: базовая 1.0 + штраф за горячую клетку
            float heatPenalty = fm.at(nx,ny) * 3.0f;  // горячие клетки = дорогие
            float moveCost    = 1.0f + heatPenalty + noise(rng);
            float newCost     = cost[flat] + moveCost;

            if (newCost < cost[nflat]) {
                cost[nflat]      = newCost;
                parent[nflat]    = flat;
                parentAct[nflat] = ACTS[d];
                pq.push({newCost + heuristic(nflat), nflat});
            }
        }
    }

    // Восстановление пути
    Solution sol;
    if (cost[goalFlat] < 1e8f) {
        std::vector<Action> rev;
        int cur = goalFlat;
        while (cur != startFlat) {
            if (parent[cur]<0) break;
            rev.push_back(parentAct[cur]);
            cur = parent[cur];
        }
        std::reverse(rev.begin(), rev.end());
        sol.actions = rev;
    }
    return sol;
}

// ---------------------------------------------------------------------------
// Спиральный обход
// ---------------------------------------------------------------------------
Solution LLMAdvisor::spiralWalk(const Problem& p, unsigned seed) const {
    // Генерируем спираль от угла и перенаправляем к цели при попадании
    Solution sol;
    int x=p.startX, y=p.startY, G=p.gridSize;
    std::mt19937 rng(seed);

    int dx[]={1,0,-1,0}, dy[]={0,1,0,-1}; // правая рука
    int dir=0, steps=1, stepCount=0, turns=0;

    for (int i=0; i<RSADConfig::MAX_SOL_LEN && (x!=p.goalX||y!=p.goalY); i++) {
        int nx=x+dx[dir], ny=y+dy[dir];
        if (nx<0||nx>=G||ny<0||ny>=G||p.isObstacle(nx,ny)) {
            dir=(dir+1)%4; stepCount=0;
            nx=x+dx[dir]; ny=y+dy[dir];
            if (nx<0||nx>=G||ny<0||ny>=G||p.isObstacle(nx,ny)) continue;
        }
        // Жадный корректор: при возможности двигаться к цели — делаем это
        if (i%3==0) {
            int bdx=p.goalX>x?1:(p.goalX<x?-1:0);
            int bdy=p.goalY>y?1:(p.goalY<y?-1:0);
            if (bdx && !p.isObstacle(x+bdx,y)) { nx=x+bdx; ny=y; }
            else if (bdy && !p.isObstacle(x,y+bdy)) { nx=x; ny=y+bdy; }
        }
        Action a;
        if (nx>x) a=Action::RIGHT; else if (nx<x) a=Action::LEFT;
        else if (ny>y) a=Action::DOWN; else a=Action::UP;
        sol.actions.push_back(a);
        x=nx; y=ny;
        stepCount++;
        if (++stepCount>=steps) { stepCount=0; if(++turns%2==0) steps++; dir=(dir+1)%4; }
    }
    return sol;
}

// ---------------------------------------------------------------------------
// Зигзаг
// ---------------------------------------------------------------------------
Solution LLMAdvisor::zigzagWalk(const Problem& p, bool vertical) const {
    Solution sol;
    int x=p.startX, y=p.startY, G=p.gridSize;
    bool forward=true;

    if (!vertical) {
        // Горизонтальные полосы: движемся по строкам
        for (int row=y; row<=p.goalY && (int)sol.actions.size()<RSADConfig::MAX_SOL_LEN; row++) {
            if (row!=y) { // переходим на следующую строку
                if (!p.isObstacle(x, row)) { sol.actions.push_back(Action::DOWN); y=row; }
                else { break; }
            }
            int targetX = forward ? G-1 : 0;
            while (x!=targetX && (int)sol.actions.size()<RSADConfig::MAX_SOL_LEN) {
                int nx=x+(forward?1:-1);
                if (p.isObstacle(nx,y)) break;
                sol.actions.push_back(forward?Action::RIGHT:Action::LEFT);
                x=nx;
                if (x==p.goalX && y==p.goalY) return sol;
            }
            forward=!forward;
        }
    } else {
        // Вертикальные полосы
        for (int col=x; col<=p.goalX && (int)sol.actions.size()<RSADConfig::MAX_SOL_LEN; col++) {
            if (col!=x) {
                if (!p.isObstacle(col,y)) { sol.actions.push_back(Action::RIGHT); x=col; }
                else break;
            }
            int targetY = forward ? G-1 : 0;
            while (y!=targetY && (int)sol.actions.size()<RSADConfig::MAX_SOL_LEN) {
                int ny=y+(forward?1:-1);
                if (p.isObstacle(x,ny)) break;
                sol.actions.push_back(forward?Action::DOWN:Action::UP);
                y=ny;
                if (x==p.goalX && y==p.goalY) return sol;
            }
            forward=!forward;
        }
    }
    return sol;
}

// ---------------------------------------------------------------------------
// A*-подобный поиск с весом препятствий
// ---------------------------------------------------------------------------
Solution LLMAdvisor::heuristicSearch(
    const Problem& p, float obsWeight, unsigned seed) const
{
    // Используем generateFailureAware с нулевой картой неудач + случайный шум
    FailureMap emptyFm;
    emptyFm.width=p.gridSize; emptyFm.height=p.gridSize;
    emptyFm.heatmap.assign(p.gridSize*p.gridSize, 0.0f);
    // Добавляем небольшой шум по клеткам рядом с препятствиями
    for (int y=0;y<p.gridSize;y++) for (int x=0;x<p.gridSize;x++) {
        if (p.isObstacle(x,y)) {
            for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
                int nx=x+dx,ny=y+dy;
                if (nx>=0&&nx<p.gridSize&&ny>=0&&ny<p.gridSize)
                    emptyFm.heatmap[ny*p.gridSize+nx] += obsWeight*0.3f;
            }
        }
    }
    emptyFm.worstX=p.gridSize/2; emptyFm.worstY=p.gridSize/2;
    return generateFailureAware(p, emptyFm, seed);
}

// ---------------------------------------------------------------------------
// Декомпозиция на квадранты
// ---------------------------------------------------------------------------
Solution LLMAdvisor::quadrantDecompose(const Problem& p, unsigned seed) const {
    int G=p.gridSize;
    // Разбиваем на 4 квадранта, находим путь через центр каждого
    std::vector<std::pair<int,int>> waypoints;
    waypoints.push_back({G/4,   G/4});
    waypoints.push_back({3*G/4, G/4});
    waypoints.push_back({3*G/4, 3*G/4});

    // Убираем точки, попавшие в препятствия
    auto free = [&](std::pair<int,int> pt) {
        int x=pt.first, y=pt.second;
        x=std::max(0,std::min(G-1,x)); y=std::max(0,std::min(G-1,y));
        for (int r=0;r<G;r++)
            for (int dy=-r;dy<=r;dy++) for (int dx=-r;dx<=r;dx++) {
                int nx=x+dx,ny=y+dy;
                if(nx<0||nx>=G||ny<0||ny>=G)continue;
                if(!p.isObstacle(nx,ny))return std::make_pair(nx,ny);
            }
        return std::make_pair(p.startX,p.startY);
    };

    Solution sol;
    std::vector<std::pair<int,int>> all = {{p.startX,p.startY}};
    for (auto& wp : waypoints) all.push_back(free(wp));
    all.push_back({p.goalX,p.goalY});

    int cx=p.startX, cy=p.startY;
    for (int i=1;i<(int)all.size();i++) {
        auto [gx,gy]=all[i];
        auto seg=world_bfs(cx,cy,gx,gy,p);
        sol.actions.insert(sol.actions.end(),seg.begin(),seg.end());
        cx=gx; cy=gy;
        if(cx==p.goalX&&cy==p.goalY)break;
    }
    return sol;
}

// Вспомогательный BFS без класса WorldModel
std::vector<Action> LLMAdvisor::world_bfs(int fx,int fy,int tx,int ty,const Problem& p) const { return world_bfs_impl(fx,fy,tx,ty,p); }

std::vector<Action> LLMAdvisor::world_bfs_impl(
    int fx,int fy,int tx,int ty,const Problem& p) const
{
    int G=p.gridSize,GG=G*G;
    std::vector<int> dist(GG,-1);
    std::vector<int> par(GG,-1);
    std::vector<Action> parAct(GG,Action::NONE);
    int sf=fy*G+fx, ef=ty*G+tx;
    dist[sf]=0;
    std::queue<int> q; q.push(sf);
    const int DX[]={0,0,-1,1},DY[]={-1,1,0,0};
    const Action A[]={Action::UP,Action::DOWN,Action::LEFT,Action::RIGHT};
    while(!q.empty()){
        int flat=q.front();q.pop();
        if(flat==ef)break;
        int cx=flat%G,cy=flat/G;
        for(int d=0;d<4;d++){
            int nx=cx+DX[d],ny=cy+DY[d];
            if(nx<0||nx>=G||ny<0||ny>=G||p.isObstacle(nx,ny))continue;
            int nf=ny*G+nx;
            if(dist[nf]!=-1)continue;
            dist[nf]=dist[flat]+1;
            par[nf]=flat; parAct[nf]=A[d];
            q.push(nf);
        }
    }
    if(dist[ef]<0)return {};
    std::vector<Action> rev;
    int cur=ef;
    while(cur!=sf){if(par[cur]<0)break;rev.push_back(parAct[cur]);cur=par[cur];}
    std::reverse(rev.begin(),rev.end());
    return rev;
}

// ---------------------------------------------------------------------------
// Парсинг ответа LLM
// ---------------------------------------------------------------------------
Solution LLMAdvisor::parseLLMResponse(const std::string& resp,
                                        const Problem& problem) const {
    Solution sol;
    for (char c : resp) {
        char cu = toupper(c);
        Action a = charToAction(cu);
        if (a != Action::NONE) sol.actions.push_back(a);
        if ((int)sol.actions.size() >= RSADConfig::MAX_SOL_LEN) break;
    }
    return sol;
}

// ---------------------------------------------------------------------------
// Режим 1: Реальный LLM
// ---------------------------------------------------------------------------
std::vector<Solution> LLMAdvisor::queryRealLLM(
    const std::vector<Solution>& pool,
    const Problem& problem,
    int budget, unsigned seed)
{
    std::vector<Solution> results;
    if (!hasRealLLM_ || !llmCallback_) return results;

    // Строим контекстный промпт
    std::ostringstream prompt;
    prompt << "=== RSAD WorldAgent ===\n";
    prompt << describePool(pool, problem) << "\n\n";

    // Добавляем карту мира в ASCII
    prompt << "Карта (S=старт G=цель #=препятствие):\n";
    for (int y=0;y<problem.gridSize;y++) {
        for (int x=0;x<problem.gridSize;x++) {
            if (x==problem.startX&&y==problem.startY) prompt<<'S';
            else if (x==problem.goalX&&y==problem.goalY) prompt<<'G';
            else if (problem.isObstacle(x,y)) prompt<<'#';
            else prompt<<'.';
        }
        prompt<<'\n';
    }

    // Лучшее известное решение как пример
    if (!pool.empty() && !pool[0].actions.empty()) {
        prompt << "\nЛучшее известное решение: " << pool[0].toString() << "\n";
        prompt << "Предложи " << budget << " альтернативных маршрутов.\n";
        prompt << "Формат: каждый маршрут на новой строке, только U/D/L/R.\n";
    }

    std::string resp = llmCallback_(prompt.str());
    RSAD_LOG("[LLMAdvisor] LLM response length=%zu\n", resp.size());

    // Парсим по строкам
    std::istringstream ss(resp);
    std::string line;
    while (std::getline(ss, line) && (int)results.size()<budget) {
        Solution s = parseLLMResponse(line, problem);
        if (!s.actions.empty()) results.push_back(s);
    }
    // Если одна строка — пробуем весь ответ
    if (results.empty()) {
        Solution s = parseLLMResponse(resp, problem);
        if (!s.actions.empty()) results.push_back(s);
    }

    RSAD_LOG("[LLMAdvisor] Real LLM generated %zu solutions\n", results.size());
    return results;
}

// ---------------------------------------------------------------------------
// Режим 2: Статистический LLM (главный fallback)
// ---------------------------------------------------------------------------
std::vector<Solution> LLMAdvisor::statisticalAdvise(
    const std::vector<Solution>& pool,
    const Problem& problem,
    int budget, unsigned seed)
{
    FailureMap fm = buildFailureMap(pool, problem);

    RSAD_LOG("[LLMAdvisor] FailureMap: worst region=(%d,%d), heat=%.2f\n",
             fm.worstX, fm.worstY,
             fm.worstX<fm.width&&fm.worstY<fm.height ?
             fm.heatmap[fm.worstY*fm.width+fm.worstX] : 0.0f);

    std::vector<Solution> results;
    std::mt19937 rng(seed);

    // Генерируем N решений с разными вариациями уклонения от проблемных регионов
    for (int i=0; i<budget && (int)results.size()<budget; i++) {
        Solution s = generateFailureAware(problem, fm, seed + i*7);
        if (!s.actions.empty()) results.push_back(s);
    }

    return results;
}

// ---------------------------------------------------------------------------
// Режим 3: Prompted search (именованные стратегии)
// ---------------------------------------------------------------------------
std::vector<Solution> LLMAdvisor::promptedSearch(
    const Problem& problem,
    const FailureMap& fm,
    int budget, unsigned seed)
{
    std::vector<Solution> results;
    std::mt19937 rng(seed);

    // Запускаем каждую стратегию-"промпт"
    std::vector<Solution> candidates;
    candidates.push_back(spiralWalk(problem, seed));
    candidates.push_back(zigzagWalk(problem, false));
    candidates.push_back(zigzagWalk(problem, true));
    candidates.push_back(heuristicSearch(problem, 1.0f, seed));
    candidates.push_back(heuristicSearch(problem, 2.5f, seed+1));
    candidates.push_back(generateFailureAware(problem, fm, seed));

    for (auto& s : candidates) {
        if (!s.actions.empty()) results.push_back(s);
        if ((int)results.size()>=budget) break;
    }

    // Если нужно больше — комбинируем стратегии
    while ((int)results.size()<budget && results.size()>=2) {
        // Crossover двух случайных стратегий
        std::uniform_int_distribution<int> ri(0,(int)results.size()-1);
        auto& p1=results[ri(rng)]; auto& p2=results[ri(rng)];
        if (p1.actions.empty()||p2.actions.empty()) break;
        std::uniform_int_distribution<int> cut1(0,(int)p1.actions.size()-1);
        std::uniform_int_distribution<int> cut2(0,(int)p2.actions.size()-1);
        Solution hybrid;
        hybrid.actions.insert(hybrid.actions.end(),
                               p1.actions.begin(), p1.actions.begin()+cut1(rng));
        hybrid.actions.insert(hybrid.actions.end(),
                               p2.actions.begin()+cut2(rng), p2.actions.end());
        if(!hybrid.actions.empty()) results.push_back(hybrid);
    }

    return results;
}

// ---------------------------------------------------------------------------
// Главный метод advise()
// ---------------------------------------------------------------------------
std::vector<Solution> LLMAdvisor::advise(
    const std::vector<Solution>& pool,
    const Problem& problem,
    int budget,
    unsigned seed)
{
    RSAD_LOG("[LLMAdvisor] advise() budget=%d pool=%zu hasLLM=%s\n",
             budget, pool.size(), hasRealLLM_?"YES":"NO");

    std::vector<Solution> allResults;

    // Режим 1: Реальный LLM (если доступен)
    if (hasRealLLM_) {
        auto r1 = queryRealLLM(pool, problem, budget/2, seed);
        allResults.insert(allResults.end(), r1.begin(), r1.end());
    }

    // Режим 2: Статистический анализ пула
    int statBudget = budget - (int)allResults.size();
    if (statBudget > 0) {
        auto r2 = statisticalAdvise(pool, problem, statBudget, seed+100);
        allResults.insert(allResults.end(), r2.begin(), r2.end());
    }

    // Режим 3: Prompted search (если ещё нужны решения)
    int promBudget = budget - (int)allResults.size();
    if (promBudget > 0) {
        FailureMap fm = buildFailureMap(pool, problem);
        auto r3 = promptedSearch(problem, fm, promBudget, seed+200);
        allResults.insert(allResults.end(), r3.begin(), r3.end());
    }

    RSAD_LOG("[LLMAdvisor] Total solutions generated: %zu\n", allResults.size());
    return allResults;
}
