// =============================================================================
// CodeGenerator.cpp — Генерация C++ кода для симуляций
// =============================================================================
#include "CodeGenerator.h"
#include <iomanip>
#include <sstream>
#include <sstream>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "platform_compat.h"

// Helper: format float ensuring decimal point
static std::string flt(float v) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(4) << v;
    // Remove trailing zeros but keep at least ".0"
    std::string s = o.str();
    auto dot = s.find('.');
    if (dot != std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last == dot) last++;
        s = s.substr(0, last+1);
    }
    return s;
}



CodeGenerator::CodeGenerator() : outputDir_("/tmp/rsad_sims") {
    mkdir(outputDir_.c_str(), 0755);
}

// ---------------------------------------------------------------------------
// Общий заголовок (вставляется в каждый файл)
// ---------------------------------------------------------------------------
std::string CodeGenerator::commonHeader(const SimulationSpec& s) const {
    std::ostringstream ss;
    ss << "// ============================================================\n";
    ss << "// Симуляция: " << s.title << "\n";
    ss << "// Тип: "       << simTypeName(s.type) << "\n";
    ss << "// Автор: "     << s.author << "\n";
    ss << "// Сгенерировано РСАД — WorldInventor\n";
    ss << "// ============================================================\n";
    ss << "#include <cstdio>\n#include <cstdlib>\n#include <cstring>\n";
    ss << "#include <cmath>\n#include <ctime>\n#include <vector>\n";
    ss << "#include <string>\n#include <algorithm>\n#include <random>\n";
    ss << "#include <unistd.h>\n\n";
    ss << ansiHelpers();
    ss << sleepHelper();
    ss << "\nstatic const int W = " << s.width  << ";\n";
    ss << "static const int H = " << s.height << ";\n";
    ss << "static const int FRAMES = " << s.frames << ";\n";
    ss << "static const int DELAY_MS = " << s.delayMs << ";\n\n";
    return ss.str();
}

std::string CodeGenerator::ansiHelpers() const {
    return R"(
// ANSI helpers
#define CLEAR()     printf("\033[2J\033[H")
#define HIDE_CURSOR() printf("\033[?25l")
#define SHOW_CURSOR() printf("\033[?25h")
#define MOVE(r,c)   printf("\033[%d;%dH", (r), (c))
#define CLR_FG(c)   printf("\033[%dm", (c))
#define CLR_BG(c)   printf("\033[%dm", (c)+10)
#define CLR_BOLD()  printf("\033[1m")
#define CLR_RESET() printf("\033[0m")
// Цвета FG: 30=black 31=red 32=green 33=yellow 34=blue 35=magenta 36=cyan 37=white
// BG: +10 (40..47)
)";
}

std::string CodeGenerator::sleepHelper() const {
    return R"(
static void ms_sleep(int ms) {
    usleep(ms * 1000);
}
static void print_header(const char* title, int frame, int total) {
    MOVE(1,1); CLR_FG(36); CLR_BOLD();
    printf("[RSAD WorldSim] %s  |  Frame: %d/%d  |  Q=quit", title, frame, total);
    CLR_RESET();
}
)";
}

std::string CodeGenerator::commonFooter() const {
    return R"(
    SHOW_CURSOR();
    CLR_RESET();
    printf("\n\n[Симуляция завершена]\n");
    return 0;
}
)";
}

int CodeGenerator::countLines(const std::string& code) const {
    return (int)std::count(code.begin(), code.end(), '\n');
}

// ===========================================================================
// ── GAME OF LIFE ─────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genGameOfLife(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);

    // Правило из spec
    auto& rule = s.caRule;
    std::string birthSet, surviveSet;
    for (int b : rule.birth)   birthSet   += std::to_string(b) + ",";
    for (int sv : rule.survive) surviveSet += std::to_string(sv) + ",";
    if (!birthSet.empty())   birthSet.pop_back();
    if (!surviveSet.empty()) surviveSet.pop_back();

    // Цвет живых клеток
    int liveColor = std::get<int>(s.getParam("live_color", SimParam(32)));
    int deadColor = std::get<int>(s.getParam("dead_color", SimParam(30)));
    char liveChar = std::get<std::string>(s.getParam("live_char", SimParam(std::string("█")))).front();
    char deadChar = std::get<std::string>(s.getParam("dead_char", SimParam(std::string(" ")))).front();
    float density = std::get<float>(s.getParam("init_density", SimParam(0.35f)));

    ss << R"(
static char grid[2][H][W];
static int cur = 0;

static bool isBirth(int n) {
    int b[] = {)" << birthSet << R"(};
    for (int x : b) if (n==x) return true;
    return false;
}
static bool isSurvive(int n) {
    int sv[] = {)" << surviveSet << R"(};
    for (int x : sv) if (n==x) return true;
    return false;
}

static void init(unsigned seed) {
    std::mt19937 rng(seed);
    std::bernoulli_distribution dist()" << density << R"();
    for (int y=0;y<H;y++) for (int x=0;x<W;x++)
        grid[0][y][x] = dist(rng) ? 1 : 0;
    cur = 0;
}

static void step() {
    int nxt = 1-cur;
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        int neighbors = 0;
        for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
            if (!dx && !dy) continue;
)" << (s.wrapEdges ? "            int ny=(y+dy+H)%H, nx=(x+dx+W)%W;" : "            int ny=y+dy, nx=x+dx; if(ny<0||ny>=H||nx<0||nx>=W)continue;") << R"(
            neighbors += grid[cur][ny][nx];
        }
        char alive = grid[cur][y][x];
        if (alive) grid[nxt][y][x] = isSurvive(neighbors) ? 1 : 0;
        else       grid[nxt][y][x] = isBirth(neighbors)   ? 1 : 0;
    }
    cur = nxt;
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    for (int y=0;y<H;y++) {
        MOVE(y+2, 1);
        for (int x=0;x<W;x++) {
            if (grid[cur][y][x]) { CLR_FG()" << liveColor << R"(); putchar(')" << liveChar << R"('); }
            else                 { CLR_FG()" << deadColor << R"(); putchar(')" << deadChar << R"('); }
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR();
    init((unsigned)time(nullptr));
    for (int f=1; f<=FRAMES; f++) {
        draw(f); step(); ms_sleep(DELAY_MS);
    }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── LANGTON'S ANT ────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genLangtonAnt(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    int numAnts  = std::get<int>(s.getParam("num_ants", SimParam(3)));
    std::string rules = std::get<std::string>(s.getParam("ant_rules", SimParam(std::string("RL"))));

    ss << R"(
static int grid[H][W]; // 0..numStates-1
static const char* RULES = ")" << rules << R"("; // R=turn right, L=turn left
static const int N_STATES = )" << (int)rules.size() << R"(;

struct Ant {
    int x, y, dir; // dir: 0=N 1=E 2=S 3=W
};

static Ant ants[)" << numAnts << R"(];
static int nAnts = )" << numAnts << R"(;

static void init() {
    memset(grid, 0, sizeof(grid));
    std::mt19937 rng(42);
    for (int i=0;i<nAnts;i++) {
        ants[i].x   = W/2 + (rng()%10) - 5;
        ants[i].y   = H/2 + (rng()%6)  - 3;
        ants[i].dir = rng()%4;
    }
}

static const int DX[4] = {0,1,0,-1};
static const int DY[4] = {-1,0,1,0};

static void step() {
    for (int i=0;i<nAnts;i++) {
        Ant& a = ants[i];
        int state = grid[a.y][a.x];
        char rule = RULES[state % N_STATES];
        if (rule=='R') a.dir = (a.dir+1)%4;
        else           a.dir = (a.dir+3)%4;
        grid[a.y][a.x] = (state+1) % N_STATES;
        a.x = (a.x + DX[a.dir] + W) % W;
        a.y = (a.y + DY[a.dir] + H) % H;
    }
}

static const int COLORS[] = {30,32,33,34,35,36,37,31};

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            int st = grid[y][x];
            // Проверяем муравья
            bool isAnt = false;
            for (int i=0;i<nAnts;i++) if (ants[i].x==x && ants[i].y==y) isAnt=true;
            if (isAnt) { CLR_FG(31); CLR_BOLD(); putchar('@'); CLR_RESET(); }
            else if (st==0) { CLR_FG(30); putchar('.'); }
            else { CLR_FG(COLORS[st%8]); putchar('0'+st); }
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── PARTICLE GRAVITY ─────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genParticleGrav(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    int   nPart    = std::get<int>(s.getParam("num_particles", SimParam(80)));
    float gravity  = std::get<float>(s.getParam("gravity", SimParam(0.3f)));
    float friction = std::get<float>(s.getParam("friction", SimParam(0.98f)));
    int   nBlack   = std::get<int>(s.getParam("num_black_holes", SimParam(2)));

    ss << R"(
struct Particle {
    float x, y, vx, vy;
    int color;
    char sym;
};
struct BlackHole {
    float x, y, mass;
};

static Particle parts[)" << nPart << R"(];
static BlackHole holes[)" << nBlack << R"(];
static int nParts = )" << nPart << R"(;
static int nHoles = )" << nBlack << R"(;
static const float GRAVITY = )" << flt(gravity) << R"(f;
static const float FRICTION = )" << flt(friction) << R"(f;

static void init() {
    std::mt19937 rng(time(nullptr));
    std::uniform_real_distribution<float> rx(1, W-2), ry(1, H-2);
    std::uniform_real_distribution<float> rv(-1.5f, 1.5f);
    const int colors[] = {31,32,33,34,35,36,37};
    const char syms[] = "*+.oO@#";
    for (int i=0;i<nParts;i++) {
        parts[i] = {rx(rng), ry(rng), rv(rng), rv(rng),
                    colors[rng()%7], syms[rng()%7]};
    }
    for (int i=0;i<nHoles;i++) {
        holes[i] = {rx(rng), ry(rng), 2.0f + (rng()%3)};
    }
}

static void step() {
    for (int i=0;i<nParts;i++) {
        // Гравитация чёрных дыр
        for (int j=0;j<nHoles;j++) {
            float dx = holes[j].x - parts[i].x;
            float dy = holes[j].y - parts[i].y;
            float d2 = dx*dx + dy*dy + 0.5f;
            float force = holes[j].mass / d2;
            parts[i].vx += force * dx / sqrtf(d2);
            parts[i].vy += force * dy / sqrtf(d2);
        }
        // Глобальная гравитация вниз
        parts[i].vy += GRAVITY * 0.1f;
        // Трение
        parts[i].vx *= FRICTION;
        parts[i].vy *= FRICTION;
        // Движение
        parts[i].x += parts[i].vx;
        parts[i].y += parts[i].vy;
        // Границы (упругое отражение)
        if (parts[i].x<0.5f)   { parts[i].x=0.5f;  parts[i].vx*=-0.7f; }
        if (parts[i].x>W-1.5f) { parts[i].x=W-1.5f;parts[i].vx*=-0.7f; }
        if (parts[i].y<0.5f)   { parts[i].y=0.5f;  parts[i].vy*=-0.7f; }
        if (parts[i].y>H-1.5f) { parts[i].y=H-1.5f;parts[i].vy*=-0.7f; }
    }
}

static char buf[H][W];
static int  cbuf[H][W];

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    memset(buf, ' ', sizeof(buf));
    memset(cbuf, 0, sizeof(cbuf));
    // Чёрные дыры
    for (int j=0;j<nHoles;j++) {
        int x=(int)holes[j].x, y=(int)holes[j].y;
        if (x>=0&&x<W&&y>=0&&y<H) { buf[y][x]='*'; cbuf[y][x]=35; }
    }
    // Частицы
    for (int i=0;i<nParts;i++) {
        int x=(int)parts[i].x, y=(int)parts[i].y;
        if (x>=0&&x<W&&y>=0&&y<H) { buf[y][x]=parts[i].sym; cbuf[y][x]=parts[i].color; }
    }
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            if (cbuf[y][x]) CLR_FG(cbuf[y][x]);
            else            CLR_FG(30);
            putchar(buf[y][x]);
        }
    }
    CLR_RESET(); fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── FOREST FIRE ───────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genForestFire(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    float pTree    = std::get<float>(s.getParam("tree_prob",    SimParam(0.60f)));
    float pFire    = std::get<float>(s.getParam("fire_prob",    SimParam(0.0002f)));
    float pGrow    = std::get<float>(s.getParam("grow_prob",    SimParam(0.005f)));
    float pLightning=std::get<float>(s.getParam("lightning",    SimParam(0.00005f)));

    ss << R"(
// 0=empty 1=tree 2=burning 3=ash
static int grid[2][H][W];
static int cur=0;

static std::mt19937 rng(time(nullptr));

static void init() {
    std::bernoulli_distribution td()" << pTree << R"();
    for (int y=0;y<H;y++) for (int x=0;x<W;x++)
        grid[0][y][x] = td(rng) ? 1 : 0;
}

static void step() {
    std::uniform_real_distribution<float> ud(0,1);
    int nxt=1-cur;
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        int st = grid[cur][y][x];
        if (st==2) { grid[nxt][y][x]=3; continue; }
        if (st==3) { grid[nxt][y][x] = ud(rng)<)" << flt(pGrow) << R"(f ? 1 : 0; continue; }
        if (st==0) { grid[nxt][y][x] = ud(rng)<)" << flt(pGrow*0.5f) << R"(f ? 1 : 0; continue; }
        // tree
        bool fireNear=false;
        for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
            if (!dx&&!dy) continue;
            int ny=(y+dy+H)%H, nx=(x+dx+W)%W;
            if (grid[cur][ny][nx]==2) { fireNear=true; break; }
        }
        if (fireNear && ud(rng)<)" << flt(pFire) << R"(f*500) grid[nxt][y][x]=2;
        else if (ud(rng)<)" << flt(pLightning) << R"(f) grid[nxt][y][x]=2;
        else grid[nxt][y][x]=1;
    }
    cur=nxt;
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            switch(grid[cur][y][x]) {
                case 0: CLR_FG(30); putchar(' ');  break;
                case 1: CLR_FG(32); putchar('^');  break;  // дерево
                case 2: CLR_FG(33); CLR_BOLD(); putchar('*'); CLR_RESET(); break; // огонь
                case 3: CLR_FG(31); putchar('.');  break;  // зола
            }
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── ECOSYSTEM ────────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genEcosystem(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);

    ss << R"(
// 0=empty 1=plant 2=herbivore 3=predator
static int grid[2][H][W];
static int energy[H][W];
static int cur=0;
static std::mt19937 rng(time(nullptr));

static void init() {
    std::uniform_int_distribution<int> d(0,3);
    std::uniform_int_distribution<int> e(3,10);
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        float r=(float)(rng()%100)/100.f;
        if (r<0.35f) grid[0][y][x]=1;
        else if(r<0.50f) grid[0][y][x]=2;
        else if(r<0.55f) grid[0][y][x]=3;
        else grid[0][y][x]=0;
        energy[y][x]=e(rng);
    }
}

static void step() {
    std::uniform_int_distribution<int> d4(0,3);
    std::uniform_real_distribution<float> ud(0,1);
    int nxt=1-cur;
    // Копируем
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) grid[nxt][y][x]=grid[cur][y][x];
    const int DX[]={0,0,-1,1}, DY[]={-1,1,0,0};

    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        int st=grid[cur][y][x];
        if (st==0) {
            if (ud(rng)<0.01f) grid[nxt][y][x]=1; // спонтанный рост
            continue;
        }
        if (st==1) { // растение
            if (ud(rng)<0.005f) grid[nxt][y][x]=0; // отмирает
            // Размножение в пустую соседнюю клетку
            if (ud(rng)<0.03f) {
                int d=d4(rng);
                int nx=(x+DX[d]+W)%W, ny=(y+DY[d]+H)%H;
                if (grid[cur][ny][nx]==0) grid[nxt][ny][nx]=1;
            }
            continue;
        }
        if (st==2) { // травоядное
            energy[y][x]--;
            if (energy[y][x]<=0) { grid[nxt][y][x]=0; continue; }
            // Ищем растение рядом
            bool ate=false;
            for (int d=0;d<4&&!ate;d++) {
                int nx=(x+DX[d]+W)%W, ny=(y+DY[d]+H)%H;
                if (grid[cur][ny][nx]==1) {
                    grid[nxt][y][x]=0; grid[nxt][ny][nx]=2;
                    energy[ny][nx]=std::min(15,energy[y][x]+4);
                    ate=true;
                }
            }
            if (!ate && ud(rng)<0.015f) { // размножение
                int d=d4(rng);
                int nx=(x+DX[d]+W)%W, ny=(y+DY[d]+H)%H;
                if (grid[cur][ny][nx]==0) { grid[nxt][ny][nx]=2; energy[ny][nx]=5; }
            }
            continue;
        }
        if (st==3) { // хищник
            energy[y][x]--;
            if (energy[y][x]<=0) { grid[nxt][y][x]=0; continue; }
            bool ate=false;
            for (int d=0;d<4&&!ate;d++) {
                int nx=(x+DX[d]+W)%W, ny=(y+DY[d]+H)%H;
                if (grid[cur][ny][nx]==2) {
                    grid[nxt][y][x]=0; grid[nxt][ny][nx]=3;
                    energy[ny][nx]=std::min(20,energy[y][x]+6);
                    ate=true;
                }
            }
            if (!ate && ud(rng)<0.008f) {
                int d=d4(rng);
                int nx=(x+DX[d]+W)%W, ny=(y+DY[d]+H)%H;
                if (grid[cur][ny][nx]==0) { grid[nxt][ny][nx]=3; energy[ny][nx]=8; }
            }
        }
    }
    cur=nxt;
}

static void draw(int frame) {
    // Подсчёт популяций
    int cnt[4]={};
    for(int y=0;y<H;y++) for(int x=0;x<W;x++) cnt[grid[cur][y][x]]++;
    print_header(")" << s.title << R"(", frame, FRAMES);
    MOVE(1,50); CLR_FG(32); printf(" Plants:%-4d",cnt[1]);
    MOVE(1,62); CLR_FG(33); printf(" Herb:%-4d",cnt[2]);
    MOVE(1,72); CLR_FG(31); printf(" Pred:%-3d",cnt[3]);
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            switch(grid[cur][y][x]) {
                case 0: CLR_FG(30); putchar(' '); break;
                case 1: CLR_FG(32); putchar('"'); break;  // растение
                case 2: CLR_FG(33); putchar('o'); break;  // травоядное
                case 3: CLR_FG(31); CLR_BOLD(); putchar('W'); CLR_RESET(); break; // хищник
            }
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── WAVE ─────────────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genWave(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    float damping = std::get<float>(s.getParam("damping", SimParam(0.995f)));
    int   nSources= std::get<int>(s.getParam("num_sources", SimParam(3)));

    ss << R"(
static float u[3][H][W]; // u[0]=cur, u[1]=prev, u[2]=next
static int t_idx = 0;
static const float DAMPING = )" << flt(damping) << R"(f;
static const float C = 0.5f; // скорость волны

struct Source { int x, y; float freq, phase; };
static Source sources[)" << nSources << R"(];

static void init() {
    memset(u, 0, sizeof(u));
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> rx(W/6, 5*W/6), ry(H/6, 5*H/6);
    std::uniform_real_distribution<float> rf(0.05f, 0.25f), rp(0, 6.28f);
    for (int i=0;i<)" << nSources << R"(;i++)
        sources[i] = {rx(rng), ry(rng), rf(rng), rp(rng)};
}

static void step(int frame) {
    int c=t_idx, p=(t_idx+2)%3, n=(t_idx+1)%3;
    // Волновое уравнение: u_next = 2*u_cur - u_prev + C^2*(laplacian)
    for (int y=1;y<H-1;y++) for (int x=1;x<W-1;x++) {
        float lap = u[c][y-1][x]+u[c][y+1][x]+u[c][y][x-1]+u[c][y][x+1] - 4*u[c][y][x];
        u[n][y][x] = (2*u[c][y][x] - u[p][y][x] + C*C*lap) * DAMPING;
    }
    // Источники
    float ft = frame * 0.1f;
    for (int i=0;i<)" << nSources << R"(;i++)
        u[n][sources[i].y][sources[i].x] = 2.5f * sinf(sources[i].freq*ft + sources[i].phase);
    t_idx = n;
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    int c = t_idx;
    const char* grad = " .,:;!|l1ILJYVU0OQ#";
    int glen = (int)strlen(grad);
    const int waveColors[] = {34, 36, 35, 33};
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            float v = u[c][y][x];
            int idx = (int)((v + 2.5f) / 5.0f * (glen-1));
            idx = std::max(0, std::min(glen-1, idx));
            int col = waveColors[idx % 4];
            CLR_FG(col);
            putchar(grad[idx]);
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(f); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── CRYSTAL (DLA) ────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genCrystal(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    int   nSeeds    = std::get<int>(s.getParam("num_seeds", SimParam(3)));
    int   nWalkers  = std::get<int>(s.getParam("num_walkers", SimParam(200)));
    float stickProb = std::get<float>(s.getParam("stick_prob", SimParam(0.9f)));

    ss << R"(
static int crystal[H][W]; // 0=empty, 1..N=crystal age
static int age = 0;
static std::mt19937 rng(time(nullptr));

struct Walker { float x, y; int color; };
static std::vector<Walker> walkers;
static const float STICK = )" << flt(stickProb) << R"(f;

static void init() {
    memset(crystal,0,sizeof(crystal));
    // Начальные затравки
    for (int i=0;i<)" << nSeeds << R"(;i++) {
        int sx = rng()%W, sy = rng()%H;
        crystal[sy][sx] = 1;
    }
    // Затравка по центру
    crystal[H/2][W/2] = 1;

    std::uniform_real_distribution<float> rx(0,W), ry(0,H);
    const int colors[]={31,32,33,34,35,36,37};
    for (int i=0;i<)" << nWalkers << R"(;i++)
        walkers.push_back({rx(rng), ry(rng), colors[rng()%7]});
}

static bool hasNeighbor(int x, int y) {
    for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
        if (!dx&&!dy) continue;
        int ny=(y+dy+H)%H, nx=(x+dx+W)%W;
        if (crystal[ny][nx]) return true;
    }
    return false;
}

static void step() {
    std::uniform_real_distribution<float> ud(0,1);
    std::uniform_int_distribution<int> dirD(0,3);
    const float DX[]={0,0,-1,1}, DY[]={-1,1,0,0};
    age++;

    for (auto& w : walkers) {
        // Случайное блуждание
        int d = dirD(rng);
        w.x += DX[d]; w.y += DY[d];
        w.x = fmodf(w.x+W, W); w.y = fmodf(w.y+H, H);
        int ix=(int)w.x, iy=(int)w.y;
        if (!crystal[iy][ix] && hasNeighbor(ix,iy) && ud(rng)<STICK) {
            crystal[iy][ix] = age;
            // Добавить нового блуждальщика
            std::uniform_real_distribution<float> rx(0,W),ry(0,H);
            const int colors[]={31,32,33,34,35,36,37};
            walkers.push_back({rx(rng),ry(rng),(int)colors[rng()%7]});
            w.x=rx(rng); w.y=ry(rng); // переместить текущего
        }
    }
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    const int colors[]={30,34,36,32,33,35,31,37};
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            int c = crystal[y][x];
            if (c==0) { CLR_FG(30); putchar(' '); }
            else {
                CLR_FG(colors[c%8]);
                int brightness = (c*7/std::max(1,age));
                putchar(".,:;!|#*"[std::min(7,brightness)]);
            }
        }
    }
    // Блуждальщики
    for (auto& w : walkers) {
        int ix=(int)w.x, iy=(int)w.y;
        if (ix>=0&&ix<W&&iy>=0&&iy<H) {
            MOVE(iy+2,ix+1); CLR_FG(w.color); putchar('.');
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── NEURAL FIRE ──────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genNeuralFire(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    int   refract   = std::get<int>(s.getParam("refractory", SimParam(5)));
    float threshold = std::get<float>(s.getParam("threshold", SimParam(0.6f)));
    float excite    = std::get<float>(s.getParam("excitation", SimParam(1.0f)));

    ss << R"(
// Нейронные состояния: 0=покой, 1=возбуждён, 2..R=рефрактерный период
static int grid[2][H][W];
static float potential[H][W];
static int cur=0;
static const int REFRACT = )" << refract << R"(;
static const float THRESH = )" << flt(threshold) << R"(f;
static const float EXCITE = )" << flt(excite) << R"(f;
static std::mt19937 rng(time(nullptr));

static void init() {
    std::bernoulli_distribution bd(0.1f);
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        grid[0][y][x] = bd(rng) ? 1 : 0;
        potential[y][x] = 0;
    }
}

static void step() {
    std::uniform_real_distribution<float> ud(0,1);
    int nxt=1-cur;
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        int st=grid[cur][y][x];
        if (st==1) { grid[nxt][y][x]=2; potential[y][x]=0; continue; }
        if (st>=2) { grid[nxt][y][x]= (st<REFRACT) ? st+1 : 0; continue; }
        // Подсчёт возбуждённых соседей
        float inp=0;
        for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
            if(!dx&&!dy) continue;
            int ny=(y+dy+H)%H, nx=(x+dx+W)%W;
            if (grid[cur][ny][nx]==1) inp += EXCITE * 0.15f;
        }
        potential[y][x] += inp;
        potential[y][x] *= 0.9f; // утечка
        // Спонтанная активация
        if (ud(rng)<0.0003f) potential[y][x]=THRESH;
        grid[nxt][y][x] = (potential[y][x] >= THRESH) ? 1 : 0;
    }
    cur=nxt;
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            int st=grid[cur][y][x];
            float p=potential[y][x];
            if (st==1) { CLR_FG(33); CLR_BOLD(); putchar('O'); CLR_RESET(); } // пик
            else if (st>=2) {                                                    // рефрактерный
                int c = 31 + (REFRACT-st)*2/REFRACT;
                CLR_FG(std::min(37,c)); putchar('o');
            } else if (p>0.1f) {                                                // нарастание
                int lvl=(int)(p/THRESH*4);
                CLR_FG(34+lvl%3); putchar(".,:!"[std::min(3,lvl)]);
            } else { CLR_FG(30); putchar(' '); }
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── MATRIX RAIN ──────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genMatrixRain(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    float density = std::get<float>(s.getParam("density",    SimParam(0.04f)));
    int   trailLen= std::get<int>(s.getParam("trail_length", SimParam(12)));

    ss << R"(
struct Drop {
    int x, y;
    int speed;   // 1..3: кадров между шагами
    int timer;
    int trail;   // длина следа
    std::vector<int> hist; // история y
};

static std::vector<Drop> drops;
static std::mt19937 rng(time(nullptr));
static int TRAIL = )" << trailLen << R"(;
static float DENSITY = )" << flt(density) << R"(f;

static const char* KANA =
    "abcdefghijklmnopqrstuvwxyz0123456789@#$%&*+=<>[]{}";

static char randChar() {
    return KANA[rng() % strlen(KANA)];
}

// Буфер символов и яркости
static char  cbuf[H][W];
static int   bright[H][W]; // 0=off, 1..TRAIL

static void init() {
    memset(cbuf,' ',sizeof(cbuf));
    memset(bright,0,sizeof(bright));
    std::uniform_int_distribution<int> rx(0,W-1);
    std::uniform_int_distribution<int> ry(0,H-1);
    std::uniform_int_distribution<int> rs(1,3);
    int n = (int)(W * DENSITY * 10);
    for (int i=0;i<n;i++)
        drops.push_back({rx(rng), ry(rng), rs(rng), 0, TRAIL, {}});
}

static void step(int frame) {
    // Затухание следов
    for (int y=0;y<H;y++) for (int x=0;x<W;x++)
        if (bright[y][x]>0) bright[y][x]--;

    // Обновление капель
    std::uniform_real_distribution<float> ud(0,1);
    std::uniform_int_distribution<int> rx(0,W-1);
    std::uniform_int_distribution<int> rs(1,3);
    for (auto& d : drops) {
        d.timer++;
        if (d.timer < d.speed) continue;
        d.timer=0;
        d.y++;
        if (d.y >= H) {
            d.y=0; d.x=rx(rng); d.speed=rs(rng);
            d.hist.clear();
        }
        // Рисуем голову
        cbuf[d.y][d.x] = randChar();
        bright[d.y][d.x] = d.trail+2; // голова ярче
        d.hist.push_back(d.y);
        if ((int)d.hist.size()>d.trail) d.hist.erase(d.hist.begin());
        // Случайная смена символа в следе
        if (!d.hist.empty()) {
            int ty = d.hist[0];
            if (ty>=0&&ty<H) cbuf[ty][d.x]=randChar();
        }
    }
    // Новые капли
    if (ud(rng)<DENSITY) {
        std::uniform_int_distribution<int> rx2(0,W-1);
        drops.push_back({rx2(rng),0,rs(rng),0,TRAIL,{}});
    }
}

static void draw(int frame) {
    MOVE(1,1); CLR_FG(32); CLR_BOLD();
    printf("[RSAD] MATRIX RAIN | Frame: %d/%d | The Matrix has you...", frame, FRAMES);
    CLR_RESET();
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            int b = bright[y][x];
            char c = cbuf[y][x];
            if (b==0) { CLR_FG(30); putchar(' '); }
            else if (b>=TRAIL) { CLR_FG(37); CLR_BOLD(); putchar(c); CLR_RESET(); } // голова
            else if (b>=TRAIL/2) { CLR_FG(32); putchar(c); }  // яркий след
            else { CLR_FG(32); printf("\033[2m"); putchar(c); CLR_RESET(); } // тусклый
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(f); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── FLUID CA ─────────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genFluidCA(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    float viscosity = std::get<float>(s.getParam("viscosity", SimParam(0.5f)));
    int   nSources  = std::get<int>(s.getParam("num_sources", SimParam(2)));

    ss << R"(
// Простая жидкостная CA: каждая клетка хранит плотность жидкости
static float density[2][H][W];
static float vel_x[H][W], vel_y[H][W];
static int cur=0;
static const float VIS = )" << flt(viscosity) << R"(f;

struct FSource { int x,y; float strength; };
static FSource sources[)" << nSources << R"(];
static int nSrc=)" << nSources << R"(;

static std::mt19937 rng(42);

static void init() {
    memset(density,0,sizeof(density));
    memset(vel_x,0,sizeof(vel_x));
    memset(vel_y,0,sizeof(vel_y));
    std::uniform_int_distribution<int> rx(W/4,3*W/4), ry(1,H/3);
    std::uniform_real_distribution<float> rs(0.5f,1.0f);
    for (int i=0;i<nSrc;i++)
        sources[i]={rx(rng),ry(rng),rs(rng)};
}

static void step() {
    int nxt=1-cur;
    memset(density[nxt],0,sizeof(density[nxt]));
    // Источники жидкости
    for (int i=0;i<nSrc;i++)
        density[cur][sources[i].y][sources[i].x] += sources[i].strength;
    // Гравитация вниз + вязкость
    for (int y=0;y<H-1;y++) for (int x=0;x<W;x++) {
        float d=density[cur][y][x];
        if (d<0.001f) continue;
        // Течёт вниз
        float toDown = std::min(d, 1.0f - density[cur][y+1][x]);
        toDown = std::max(0.f, toDown * 0.8f);
        density[nxt][y+1][x] += toDown;
        density[nxt][y][x]   += d - toDown;
        // Растекается в стороны
        float side=d*0.1f;
        if (x>0)   density[nxt][y][x-1] += side;
        if (x<W-1) density[nxt][y][x+1] += side;
        density[nxt][y][x] -= side*2;
    }
    // Клэмп
    for (int y=0;y<H;y++) for (int x=0;x<W;x++)
        density[nxt][y][x]=std::max(0.f,std::min(2.f,density[nxt][y][x]));
    cur=nxt;
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    const char* lev=" .,:;!|1ILJYV0O#@";
    int glen=(int)strlen(lev);
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            float d=density[cur][y][x];
            int idx=(int)(d/2.0f*(glen-1));
            idx=std::max(0,std::min(glen-1,idx));
            // Цвет: синий→голубой→белый
            if (idx<3)       CLR_FG(30);
            else if(idx<glen/3) CLR_FG(34);
            else if(idx<2*glen/3) CLR_FG(36);
            else             { CLR_FG(37); CLR_BOLD(); }
            putchar(lev[idx]);
            CLR_RESET();
        }
    }
    // Источники
    for (int i=0;i<nSrc;i++) {
        MOVE(sources[i].y+2, sources[i].x+1);
        CLR_FG(33); CLR_BOLD(); putchar('S'); CLR_RESET();
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── BOIDS ─────────────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genBoids(const SimulationSpec& s) {
    std::ostringstream ss;
    ss << commonHeader(s);
    int   nBoids    = std::get<int>(s.getParam("num_boids",   SimParam(60)));
    float sepWeight = std::get<float>(s.getParam("separation", SimParam(1.5f)));
    float aliWeight = std::get<float>(s.getParam("alignment",  SimParam(1.0f)));
    float cohWeight = std::get<float>(s.getParam("cohesion",   SimParam(0.8f)));
    float radius    = std::get<float>(s.getParam("radius",     SimParam(8.0f)));

    ss << R"(
struct Boid { float x,y,vx,vy; int color; char sym; };
static Boid boids[)" << nBoids << R"(];
static int nBoids=)" << nBoids << R"(;
static const float SEP=)" << flt(sepWeight) << R"(f, ALI=)" << flt(aliWeight) << R"(f, COH=)" << flt(cohWeight) << R"(f, RAD=)" << flt(radius) << R"(f;
static const float MAX_SPEED=1.5f;

static void init() {
    std::mt19937 rng(time(nullptr));
    std::uniform_real_distribution<float> rx(0,W),ry(0,H),rv(-1,1);
    const int colors[]={31,32,33,34,35,36,37};
    const char syms[]=">v<^";
    for (int i=0;i<nBoids;i++) {
        boids[i]={rx(rng),ry(rng),rv(rng),rv(rng),colors[rng()%7],syms[rng()%4]};
    }
}

static void step() {
    for (int i=0;i<nBoids;i++) {
        float sx=0,sy=0, ax=0,ay=0, cx=0,cy=0;
        int cnt=0;
        for (int j=0;j<nBoids;j++) {
            if (i==j) continue;
            float dx=boids[j].x-boids[i].x, dy=boids[j].y-boids[i].y;
            float d=sqrtf(dx*dx+dy*dy);
            if (d<RAD && d>0.01f) {
                if (d<RAD*0.3f) { sx-=dx/d; sy-=dy/d; }  // разделение
                ax+=boids[j].vx; ay+=boids[j].vy;          // выравнивание
                cx+=boids[j].x;  cy+=boids[j].y;           // сплочённость
                cnt++;
            }
        }
        if (cnt>0) {
            ax/=cnt; ay/=cnt; cx/=cnt; cy/=cnt;
            boids[i].vx += SEP*sx + ALI*(ax-boids[i].vx)*0.05f + COH*(cx-boids[i].x)*0.002f;
            boids[i].vy += SEP*sy + ALI*(ay-boids[i].vy)*0.05f + COH*(cy-boids[i].y)*0.002f;
        }
        float spd=sqrtf(boids[i].vx*boids[i].vx+boids[i].vy*boids[i].vy);
        if (spd>MAX_SPEED) { boids[i].vx*=MAX_SPEED/spd; boids[i].vy*=MAX_SPEED/spd; }
        boids[i].x=fmodf(boids[i].x+boids[i].vx+W,W);
        boids[i].y=fmodf(boids[i].y+boids[i].vy+H,H);
        // Выбор символа по направлению
        float angle=atan2f(boids[i].vy,boids[i].vx);
        int dir=(int)((angle+3.14159f)/3.14159f*2)%4;
        boids[i].sym=">v<^"[dir];
    }
}

static char canvas[H][W];
static int  colors_buf[H][W];

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    memset(canvas,' ',sizeof(canvas));
    memset(colors_buf,0,sizeof(colors_buf));
    for (int i=0;i<nBoids;i++) {
        int x=(int)boids[i].x, y=(int)boids[i].y;
        if(x>=0&&x<W&&y>=0&&y<H) { canvas[y][x]=boids[i].sym; colors_buf[y][x]=boids[i].color; }
    }
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            if (colors_buf[y][x]) { CLR_FG(colors_buf[y][x]); putchar(canvas[y][x]); }
            else { CLR_FG(30); putchar(' '); }
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) { draw(f); step(); ms_sleep(DELAY_MS); }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── HYBRID ────────────────────────────────────────────────────────────────────
// ===========================================================================
std::string CodeGenerator::genHybrid(const SimulationSpec& s) {
    // Для гибрида: берём первые два компонента и смешиваем правила
    std::ostringstream ss;
    ss << commonHeader(s);

    // Описание в шапке
    ss << "// Гибридный мир: ";
    for (auto t : s.hybridComponents) ss << simTypeName(t) << " + ";
    ss << "\n// Логика: " << s.hybridLogic << "\n\n";

    // Базовые переменные — общие для всех гибридов
    ss << R"(
// Многослойная симуляция:
// Слой A — клеточный автомат (правила Life)
// Слой B — частицы, привлекаемые к живым клеткам
// Слой C — волна, порождаемая скоплениями

static int  ca[2][H][W];     // клеточный автомат
static float wave[2][H][W];  // волна
static int  ca_cur=0, w_cur=0;
static std::mt19937 rng(time(nullptr));

struct Spark { float x,y,vx,vy; float life; int color; };
static std::vector<Spark> sparks;

static void init() {
    std::bernoulli_distribution bd(0.35f);
    for (int y=0;y<H;y++) for (int x=0;x<W;x++)
        ca[0][y][x]=bd(rng)?1:0;
    memset(wave,0,sizeof(wave));
}

static void stepCA() {
    int nxt=1-ca_cur;
    for (int y=0;y<H;y++) for (int x=0;x<W;x++) {
        int n=0;
        for (int dy=-1;dy<=1;dy++) for (int dx=-1;dx<=1;dx++) {
            if(!dx&&!dy) continue;
            n+=ca[ca_cur][(y+dy+H)%H][(x+dx+W)%W];
        }
        int alive=ca[ca_cur][y][x];
        ca[nxt][y][x]=(alive ? (n==2||n==3) : (n==3)) ? 1 : 0;
        // Рождение частицы
        if (!alive && n==3 && (rng()%20)==0) {
            std::uniform_real_distribution<float> rv(-0.5f,0.5f);
            const int cols[]={31,32,33,34,35,36,37};
            sparks.push_back({(float)x,(float)y,rv(rng),rv(rng),
                              20.f,(int)cols[rng()%7]});
        }
    }
    ca_cur=nxt;
}

static void stepWave(int frame) {
    int nxt=1-w_cur;
    float ft=frame*0.12f;
    for (int y=1;y<H-1;y++) for (int x=1;x<W-1;x++) {
        // Волновое уравнение + влияние CA
        float lap=wave[w_cur][y-1][x]+wave[w_cur][y+1][x]+
                  wave[w_cur][y][x-1]+wave[w_cur][y][x+1]-4*wave[w_cur][y][x];
        wave[nxt][y][x]=(2*wave[w_cur][y][x]-wave[1-w_cur][y][x]+0.25f*lap)*0.97f;
        if (ca[ca_cur][y][x]) wave[nxt][y][x]+=0.3f*sinf(ft);
    }
    w_cur=nxt;
}

static void stepSparks() {
    for (auto& sp : sparks) {
        sp.vy+=0.05f; sp.x+=sp.vx; sp.y+=sp.vy;
        sp.life-=1.0f;
        if(sp.x<0||sp.x>=W||sp.y<0||sp.y>=H) sp.life=0;
    }
    sparks.erase(std::remove_if(sparks.begin(),sparks.end(),
        [](const Spark&s){return s.life<=0;}),sparks.end());
}

static void draw(int frame) {
    print_header(")" << s.title << R"(", frame, FRAMES);
    for (int y=0;y<H;y++) {
        MOVE(y+2,1);
        for (int x=0;x<W;x++) {
            int alive=ca[ca_cur][y][x];
            float wv=wave[w_cur][y][x];
            if (alive) {
                int c=34+(int)(fabsf(wv)*3)%4;
                CLR_FG(c); CLR_BOLD(); putchar('#'); CLR_RESET();
            } else if (fabsf(wv)>0.2f) {
                CLR_FG(36); putchar(wv>0?'+':'-');
            } else { CLR_FG(30); putchar(' '); }
        }
    }
    for (auto& sp : sparks) {
        int ix=(int)sp.x, iy=(int)sp.y;
        if(ix>=0&&ix<W&&iy>=0&&iy<H) {
            MOVE(iy+2,ix+1);
            int bright=(int)(sp.life/20.f*7);
            CLR_FG(sp.color);
            putchar(".+*@#"[std::min(4,bright)]);
        }
    }
    fflush(stdout);
}

int main() {
    HIDE_CURSOR(); CLEAR(); init();
    for (int f=1;f<=FRAMES;f++) {
        draw(f);
        stepCA();
        stepWave(f);
        stepSparks();
        ms_sleep(DELAY_MS);
    }
)";
    ss << commonFooter();
    return ss.str();
}

// ===========================================================================
// ── ОСНОВНОЙ МЕТОД generate() ────────────────────────────────────────────────
// ===========================================================================
GeneratedCode CodeGenerator::generate(const SimulationSpec& spec,
                                       const std::string& outputDir) {
    GeneratedCode result;
    std::string dir = outputDir.empty() ? outputDir_ : outputDir;
    mkdir(dir.c_str(), 0755);

    // Генерируем код по типу
    std::string code;
    switch(spec.type) {
        case SimType::GAME_OF_LIFE:     code = genGameOfLife(spec);    break;
        case SimType::LANGTON_ANT:      code = genLangtonAnt(spec);    break;
        case SimType::PARTICLE_GRAVITY: code = genParticleGrav(spec);  break;
        case SimType::FOREST_FIRE:      code = genForestFire(spec);    break;
        case SimType::ECOSYSTEM:        code = genEcosystem(spec);     break;
        case SimType::WAVE:             code = genWave(spec);          break;
        case SimType::CRYSTAL:          code = genCrystal(spec);       break;
        case SimType::NEURAL_FIRE:      code = genNeuralFire(spec);    break;
        case SimType::MATRIX_RAIN:      code = genMatrixRain(spec);    break;
        case SimType::FLUID_CA:         code = genFluidCA(spec);       break;
        case SimType::BOIDS:            code = genBoids(spec);         break;
        case SimType::HYBRID:           code = genHybrid(spec);        break;
        default:                        code = genGameOfLife(spec);    break;
    }

    result.sourceCode  = code;
    result.linesOfCode = countLines(code);
    result.filename    = dir + "/" + spec.id;
    result.compileCmd  = "g++ -O2 -std=c++17 -o " + result.filename
                       + " " + result.filename + ".cpp -lm";
    result.runCmd      = result.filename;

    // Записываем файл
    std::ofstream f(result.filename + ".cpp");
    if (!f) {
        result.errorMsg = "Cannot write: " + result.filename + ".cpp";
        return result;
    }
    f << code;
    f.close();

    result.success = true;
    return result;
}
