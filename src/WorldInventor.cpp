// =============================================================================
// WorldInventor.cpp — Движок изобретения симуляций
// =============================================================================
#include "WorldInventor.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <sstream>
#include <ctime>

WorldInventor::WorldInventor() : rng_(42) {
    initConcepts();
    initPresets();
}

// ---------------------------------------------------------------------------
// Словарь концептов (база знаний об известных симуляциях)
// ---------------------------------------------------------------------------
void WorldInventor::initConcepts() {
    auto makeEmb = [](uint32_t seed, float* out) {
        std::mt19937 r(seed); std::normal_distribution<float> nd(0,1);
        float norm=0;
        for (int i=0;i<32;i++){out[i]=nd(r);norm+=out[i]*out[i];}
        norm=sqrtf(norm)+1e-9f;
        for(int i=0;i<32;i++) out[i]/=norm;
    };

    struct RawConcept {
        const char* name; const char* desc; SimType type;
        std::unordered_map<std::string,SimParam> params;
    };

    std::vector<RawConcept> raw = {
        {"life",          "Жизнь Конвея B3/S23",         SimType::GAME_OF_LIFE,
         {{"live_color",SimParam(32)},{"init_density",SimParam(0.35f)}}},
        {"highlife",      "HighLife B36/S23",            SimType::GAME_OF_LIFE,
         {{"live_color",SimParam(36)},{"init_density",SimParam(0.30f)}}},
        {"seeds",         "Seeds B2/S",                  SimType::GAME_OF_LIFE,
         {{"live_color",SimParam(33)},{"init_density",SimParam(0.20f)}}},
        {"daynight",      "Day & Night B3678/S34678",     SimType::GAME_OF_LIFE,
         {{"live_color",SimParam(34)},{"init_density",SimParam(0.45f)}}},
        {"langton",       "Муравей Лэнгтона RL",         SimType::LANGTON_ANT,
         {{"ant_rules",SimParam(std::string("RL"))},{"num_ants",SimParam(1)}}},
        {"turmite",       "Турмит RRLLRL",               SimType::LANGTON_ANT,
         {{"ant_rules",SimParam(std::string("RRLLRL"))},{"num_ants",SimParam(3)}}},
        {"gravity",       "Гравитация частиц",           SimType::PARTICLE_GRAVITY,
         {{"num_particles",SimParam(100)},{"gravity",SimParam(0.4f)}}},
        {"blackholes",    "Чёрные дыры",                 SimType::PARTICLE_GRAVITY,
         {{"num_particles",SimParam(150)},{"num_black_holes",SimParam(4)}}},
        {"fire",          "Лесной пожар",                SimType::FOREST_FIRE,
         {{"tree_prob",SimParam(0.65f)},{"fire_prob",SimParam(0.0003f)}}},
        {"savanna",       "Саванна (засухи)",            SimType::FOREST_FIRE,
         {{"tree_prob",SimParam(0.35f)},{"grow_prob",SimParam(0.01f)}}},
        {"ecosystem",     "Экосистема хищник/жертва",   SimType::ECOSYSTEM, {}},
        {"wave",          "Волновое уравнение",          SimType::WAVE,
         {{"num_sources",SimParam(3)},{"damping",SimParam(0.996f)}}},
        {"interference",  "Интерференция волн",         SimType::WAVE,
         {{"num_sources",SimParam(7)},{"damping",SimParam(0.999f)}}},
        {"crystal",       "Рост кристалла DLA",         SimType::CRYSTAL,
         {{"num_walkers",SimParam(300)},{"stick_prob",SimParam(0.85f)}}},
        {"snowflake",     "Снежинка",                   SimType::CRYSTAL,
         {{"num_seeds",SimParam(6)},{"stick_prob",SimParam(0.95f)}}},
        {"neural",        "Нейронные разряды",          SimType::NEURAL_FIRE,
         {{"refractory",SimParam(5)},{"threshold",SimParam(0.6f)}}},
        {"epilepsy",      "Синхронные волны",           SimType::NEURAL_FIRE,
         {{"refractory",SimParam(3)},{"threshold",SimParam(0.3f)}}},
        {"matrix",        "Цифровой дождь",             SimType::MATRIX_RAIN,
         {{"density",SimParam(0.05f)},{"trail_length",SimParam(15)}}},
        {"matrix_dense",  "Плотный Матрикс",            SimType::MATRIX_RAIN,
         {{"density",SimParam(0.12f)},{"trail_length",SimParam(8)}}},
        {"fluid",         "Жидкостная CA",              SimType::FLUID_CA,
         {{"num_sources",SimParam(3)},{"viscosity",SimParam(0.5f)}}},
        {"rain",          "Дождь",                      SimType::FLUID_CA,
         {{"num_sources",SimParam(10)},{"viscosity",SimParam(0.3f)}}},
        {"boids",         "Стая птиц",                  SimType::BOIDS,
         {{"num_boids",SimParam(80)},{"separation",SimParam(1.5f)}}},
        {"fish",          "Косяк рыб",                  SimType::BOIDS,
         {{"num_boids",SimParam(120)},{"cohesion",SimParam(1.2f)}}},
        {"hybrid_life_wave","Жизнь+Волны",              SimType::HYBRID, {}},
    };

    uint32_t seed=1;
    for (auto& r : raw) {
        SimConcept c;
        c.name = r.name; c.description = r.desc; c.baseType = r.type;
        c.defaultParams = r.params;
        makeEmb(seed++, c.embedding);
        concepts_.push_back(c);
    }
}

void WorldInventor::initPresets() {
    presetNames_ = {
        "life","highlife","seeds","daynight",
        "langton","turmite",
        "gravity","blackholes",
        "fire","savanna",
        "ecosystem",
        "wave","interference",
        "crystal","snowflake",
        "neural","epilepsy",
        "matrix","matrix_dense",
        "fluid","rain",
        "boids","fish",
        "hybrid_life_wave"
    };
}

std::vector<std::string> WorldInventor::listPresets() const {
    return presetNames_;
}

// ---------------------------------------------------------------------------
// Создание ID
// ---------------------------------------------------------------------------
std::string WorldInventor::makeId(const std::string& base) const {
    std::string safe = base;
    for (char& c : safe) if (c==' '||c=='/'||c=='\\'||c=='<'||c=='>') c='_';
    return safe + "_" + std::to_string(time(nullptr) % 10000);
}

// ---------------------------------------------------------------------------
// Определение типа симуляции из текстового запроса
// ---------------------------------------------------------------------------
SimType WorldInventor::detectTypeFromQuery(const std::string& q) {
    auto has = [&](const char* s){ return q.find(s) != std::string::npos; };

    if (has("matrix") || has("матрикс") || has("дождь цифр") || has("digital"))
        return SimType::MATRIX_RAIN;
    if (has("жизнь") || has("клетки") || has("life") || has("conway") || has("cellular"))
        return SimType::GAME_OF_LIFE;
    if (has("муравей") || has("ant") || has("langton"))
        return SimType::LANGTON_ANT;
    if (has("частиц") || has("particle") || has("гравит") || has("gravity") || has("планет"))
        return SimType::PARTICLE_GRAVITY;
    if (has("пожар") || has("огонь") || has("fire") || has("лес"))
        return SimType::FOREST_FIRE;
    if (has("экосистем") || has("хищник") || has("жертва") || has("ecosystem") || has("predator"))
        return SimType::ECOSYSTEM;
    if (has("волн") || has("wave") || has("рябь") || has("interference"))
        return SimType::WAVE;
    if (has("кристалл") || has("crystal") || has("снежинк") || has("growth") || has("DLA"))
        return SimType::CRYSTAL;
    if (has("нейрон") || has("neural") || has("мозг") || has("brain") || has("fire"))
        return SimType::NEURAL_FIRE;
    if (has("жидкость") || has("fluid") || has("вода") || has("liquid") || has("water"))
        return SimType::FLUID_CA;
    if (has("птицы") || has("boids") || has("стая") || has("рыбы") || has("flocking"))
        return SimType::BOIDS;
    if (has("гибрид") || has("hybrid") || has("комбинац") || has("mixed") || has("новый мир"))
        return SimType::HYBRID;
    // По умолчанию — случайный тип
    return static_cast<SimType>(rng_() % 11);
}

// ---------------------------------------------------------------------------
// Получение пресета по имени
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::preset(const std::string& name) {
    for (auto& c : concepts_) {
        if (c.name == name) {
            SimulationSpec spec;
            spec.id          = makeId(name);
            spec.title       = c.description;
            spec.type        = c.baseType;
            spec.author      = "preset";
            spec.description = c.description;
            spec.params      = c.defaultParams;
            spec.width       = 80; spec.height = 38;
            spec.frames      = 300; spec.delayMs = 60;

            // Настройка правил CA
            if (spec.type == SimType::GAME_OF_LIFE) {
                if (name=="life")     { spec.caRule={{3},{2,3},2,"Life B3/S23"}; }
                if (name=="highlife") { spec.caRule={{3,6},{2,3},2,"HighLife B36/S23"}; }
                if (name=="seeds")    { spec.caRule={{2},{},2,"Seeds B2/S"}; }
                if (name=="daynight") { spec.caRule={{3,6,7,8},{3,4,6,7,8},2,"DayNight"}; }
                // Символы по умолчанию
                if (!spec.hasParam("live_char"))
                    spec.setParam("live_char", SimParam(std::string("█")));
                if (!spec.hasParam("dead_char"))
                    spec.setParam("dead_char", SimParam(std::string(" ")));
            }
            if (spec.type == SimType::HYBRID) {
                spec.hybridComponents = {SimType::GAME_OF_LIFE, SimType::WAVE};
                spec.hybridLogic = "CA рождает волны, волны влияют на CA";
            }
            return spec;
        }
    }
    // Не найдено — возвращаем дефолтный Life
    return preset("life");
}

// ---------------------------------------------------------------------------
// Режим A — Аналогия: найти похожий концепт в базе и адаптировать
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::modeA_analogy(const InventionRequest& req) {
    SimType target = detectTypeFromQuery(req.userQuery);

    // Ищем концепт того же типа
    std::vector<const SimConcept*> candidates;
    for (auto& c : concepts_)
        if (c.baseType == target) candidates.push_back(&c);

    if (candidates.empty())
        for (auto& c : concepts_) candidates.push_back(&c);

    // Выбираем случайный из кандидатов
    auto& chosen = *candidates[rng_() % candidates.size()];

    SimulationSpec spec;
    spec.id          = makeId("analogy_" + std::string(simTypeName(target)));
    spec.type        = target;
    spec.author      = "WorldInventor/ModeA";
    spec.params      = chosen.defaultParams;
    spec.description = "Аналогия: " + chosen.description;
    spec.title       = "Мир-аналогия: " + std::string(simTypeName(target));
    spec.width=78; spec.height=36; spec.frames=250; spec.delayMs=70;
    spec.inspirations.push_back(chosen.name);

    // Мутируем параметры
    spec = mutateSpec(spec, 0.3f);
    spec.creativityScore = 0.3f + (rng_()%40)/100.0f;

    // CA правило
    if (target == SimType::GAME_OF_LIFE) {
        spec.caRule = chosen.name=="life" ?
            CARule{{3},{2,3},2,"Life"} : CARule{{3,6},{2,3},2,"HighLife"};
        spec.setParam("live_char", SimParam(std::string("█")));
        spec.setParam("dead_char", SimParam(std::string(" ")));
    }

    return spec;
}

// ---------------------------------------------------------------------------
// Режим B — Обратный ход: от желаемого поведения к правилам
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::modeB_backward(const InventionRequest& req) {
    auto has = [&](const char* s){ return req.userQuery.find(s) != std::string::npos; };

    // Определяем желаемое поведение
    bool wantChaos   = has("хаос") || has("chaos") || has("случайн");
    bool wantOrder   = has("порядок") || has("order") || has("симметр");
    bool wantGrowth  = has("рост") || has("growth") || has("expand");
    bool wantDecay   = has("смерть") || has("decay") || has("умира");
    bool wantFlow    = has("течет") || has("flow") || has("поток");

    SimulationSpec spec;
    spec.author = "WorldInventor/ModeB";
    spec.frames = 250; spec.delayMs = 70;
    spec.width = 78; spec.height = 36;

    if (wantFlow) {
        spec.type = SimType::FLUID_CA;
        spec.id   = makeId("backward_flow");
        spec.title = "Мир потоков";
        spec.description = "Движение жидкости, порождённое идеей потока";
        spec.setParam("num_sources", SimParam(4 + (int)(rng_()%4)));
        spec.setParam("viscosity",   SimParam(0.3f + (rng_()%4)/10.0f));
        spec.inspirations = {"поток", "жидкость"};
    } else if (wantChaos) {
        spec.type = SimType::LANGTON_ANT;
        spec.id   = makeId("backward_chaos");
        spec.title = "Мир хаоса";
        spec.description = "Детерминированный хаос из простых правил";
        const char* chaosRules[] = {"RLLRLL","RRLLRL","LRRLRL","RRRRLL"};
        spec.setParam("ant_rules", SimParam(std::string(chaosRules[rng_()%4])));
        spec.setParam("num_ants",  SimParam(2 + (int)(rng_()%5)));
        spec.inspirations = {"хаос", "детерминизм"};
    } else if (wantGrowth) {
        spec.type = SimType::CRYSTAL;
        spec.id   = makeId("backward_growth");
        spec.title = "Мир кристаллического роста";
        spec.description = "Рост структуры из случайных блужданий";
        spec.setParam("num_walkers", SimParam(200 + (int)(rng_()%200)));
        spec.setParam("stick_prob",  SimParam(0.7f + (rng_()%3)/10.0f));
        spec.inspirations = {"рост", "кристалл"};
    } else if (wantDecay) {
        spec.type = SimType::FOREST_FIRE;
        spec.id   = makeId("backward_decay");
        spec.title = "Мир угасания";
        spec.description = "Структуры возникают и разрушаются";
        spec.setParam("tree_prob", SimParam(0.7f));
        spec.setParam("fire_prob", SimParam(0.001f));
        spec.inspirations = {"распад", "угасание"};
    } else if (wantOrder) {
        spec.type = SimType::WAVE;
        spec.id   = makeId("backward_order");
        spec.title = "Мир волновой гармонии";
        spec.description = "Упорядоченные волновые паттерны";
        spec.setParam("num_sources", SimParam(2 + (int)(rng_()%3)));
        spec.setParam("damping",     SimParam(0.999f));
        spec.inspirations = {"порядок", "гармония"};
    } else {
        // По умолчанию — гибрид
        spec = inventHybrid(SimType::GAME_OF_LIFE, SimType::PARTICLE_GRAVITY);
        spec.author = "WorldInventor/ModeB";
    }

    spec.creativityScore = 0.5f + (rng_()%40)/100.0f;
    applyTheme(spec, req.theme);
    return spec;
}

// ---------------------------------------------------------------------------
// Режим C — Метафора: природный феномен → симуляция
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::modeC_metaphor(const InventionRequest& req) {
    auto has = [&](const char* s){ return req.userQuery.find(s) != std::string::npos; };

    // Каждая метафора задаётся через push_back для совместимости с unordered_map
    struct MetaEntry {
        const char* kw; const char* title; const char* desc;
        SimType type; int p_i_key; int p_i_val; const char* p_f_key; float p_f_val;
    };

    // Сначала определяем тип и параметры
    const char* keyword  = nullptr;
    const char* mTitle   = "Новый мир";
    const char* mDesc    = "Изобретённая симуляция";
    SimType mType = SimType::GAME_OF_LIFE;
    std::unordered_map<std::string,SimParam> mParams;

    if (has("космос") || has("space") || has("планет") || has("galaxy")) {
        keyword="космос"; mTitle="Галактика частиц"; mType=SimType::PARTICLE_GRAVITY;
        mDesc="Частицы вращаются вокруг звёздных масс";
        mParams["num_particles"]=SimParam(200); mParams["num_black_holes"]=SimParam(3);
        mParams["gravity"]=SimParam(0.5f); mParams["friction"]=SimParam(0.995f);
    } else if (has("мозг") || has("brain") || has("нейрон") || has("neural")) {
        keyword="мозг"; mTitle="Нейронный мозг"; mType=SimType::NEURAL_FIRE;
        mDesc="Волны возбуждения в нейронной сети";
        mParams["refractory"]=SimParam(6); mParams["threshold"]=SimParam(0.5f);
        mParams["excitation"]=SimParam(1.2f);
    } else if (has("океан") || has("ocean") || has("море") || has("sea") || has("волн")) {
        keyword="океан"; mTitle="Океанские волны"; mType=SimType::WAVE;
        mDesc="Интерференция морских волн";
        mParams["num_sources"]=SimParam(5); mParams["damping"]=SimParam(0.998f);
    } else if (has("джунгли") || has("jungle") || has("экосистем") || has("ecosystem")) {
        keyword="джунгли"; mTitle="Тропические джунгли"; mType=SimType::ECOSYSTEM;
        mDesc="Баланс хищников и жертв";
    } else if (has("вирус") || has("virus") || has("эпидем") || has("epidemic")) {
        keyword="вирус"; mTitle="Эпидемия"; mType=SimType::FOREST_FIRE;
        mDesc="Распространение вируса через экосистему";
        mParams["tree_prob"]=SimParam(0.8f); mParams["fire_prob"]=SimParam(0.002f);
        mParams["grow_prob"]=SimParam(0.001f);
    } else if (has("снег") || has("snow") || has("кристалл") || has("crystal") || has("снежинк")) {
        keyword="снег"; mTitle="Кристаллы снега"; mType=SimType::CRYSTAL;
        mDesc="Рост снежинок методом DLA";
        mParams["num_seeds"]=SimParam(8); mParams["stick_prob"]=SimParam(0.92f);
        mParams["num_walkers"]=SimParam(400);
    } else if (has("птицы") || has("birds") || has("стая") || has("flock") || has("рыбы")) {
        keyword="птицы"; mTitle="Стая скворцов"; mType=SimType::BOIDS;
        mDesc="Мурмурация птиц — стайное поведение";
        mParams["num_boids"]=SimParam(150); mParams["separation"]=SimParam(1.8f);
        mParams["cohesion"]=SimParam(0.9f);
    } else if (has("хаос") || has("chaos") || has("муравей") || has("ant")) {
        keyword="хаос"; mTitle="Хаотичный муравей"; mType=SimType::LANGTON_ANT;
        mDesc="Детерминированный хаос из простых правил";
        mParams["ant_rules"]=SimParam(std::string("RRLLRL")); mParams["num_ants"]=SimParam(3);
    } else if (has("fire") || has("огонь") || has("пожар") || has("forest")) {
        keyword="огонь"; mTitle="Лесной пожар"; mType=SimType::FOREST_FIRE;
        mDesc="Распространение огня через лес";
        mParams["tree_prob"]=SimParam(0.65f); mParams["fire_prob"]=SimParam(0.0003f);
    } else if (has("matrix") || has("матрикс") || has("код") || has("дождь")) {
        keyword="matrix"; mTitle="Цифровой дождь"; mType=SimType::MATRIX_RAIN;
        mDesc="Цифровой дождь из Матрицы";
        mParams["density"]=SimParam(0.06f); mParams["trail_length"]=SimParam(14);
    } else {
        // Случайный выбор
        SimType types[] = {SimType::WAVE, SimType::CRYSTAL, SimType::BOIDS,
                           SimType::NEURAL_FIRE, SimType::ECOSYSTEM};
        mType = types[rng_() % 5];
        keyword="random"; mTitle="Случайный мир"; mDesc="Случайно изобретённая симуляция";
    }

    SimulationSpec spec;
    spec.id = makeId(keyword ? keyword : "metaphor");
    spec.title = mTitle;
    spec.description = mDesc;
    spec.type = mType;
    spec.author = "WorldInventor/ModeC";
    spec.params = mParams;
    spec.width=80; spec.height=38; spec.frames=280; spec.delayMs=65;
    if (keyword) spec.inspirations.push_back(keyword);
    spec.creativityScore = 0.6f + (rng_()%35)/100.0f;

    if (spec.type==SimType::GAME_OF_LIFE) {
        spec.caRule={{3},{2,3},2,"Life"};
        spec.setParam("live_char",SimParam(std::string("\xe2\x96\x88")));
        spec.setParam("dead_char",SimParam(std::string(" ")));
    }
    applyTheme(spec, req.theme);
    return spec;
}

SimulationSpec WorldInventor::inventHybrid(SimType a, SimType b,
                                            const std::string& theme) {
    SimulationSpec spec;
    spec.id    = makeId("hybrid");
    spec.type  = SimType::HYBRID;
    spec.author = "WorldInventor/Hybrid";
    spec.hybridComponents = {a, b};
    spec.width=80; spec.height=38; spec.frames=300; spec.delayMs=60;

    // Описание
    std::ostringstream desc;
    desc << simTypeName(a) << " × " << simTypeName(b)
         << ": правила первого управляют вторым";
    spec.hybridLogic = desc.str();
    spec.description = spec.hybridLogic;
    spec.title = "Гибридный мир: " + std::string(simTypeName(a))
               + " + " + std::string(simTypeName(b));
    spec.inspirations = {simTypeName(a), simTypeName(b)};
    spec.creativityScore = 0.75f + (rng_()%25)/100.0f;
    applyTheme(spec, theme);
    return spec;
}

// ---------------------------------------------------------------------------
// Применение темы к параметрам
// ---------------------------------------------------------------------------
void WorldInventor::applyTheme(SimulationSpec& spec, const std::string& theme) {
    if (theme.empty()) return;
    auto has = [&](const char* s){ return theme.find(s) != std::string::npos; };

    if (has("dark") || has("тёмн")) {
        // Тёмная палитра — мало света
        spec.setParam("live_color", SimParam(34));  // синий
        spec.delayMs = 80;
    } else if (has("neon") || has("неон")) {
        spec.setParam("live_color", SimParam(35));  // пурпурный
    } else if (has("fire") || has("огонь")) {
        spec.setParam("live_color", SimParam(31));  // красный
    } else if (has("nature") || has("природа")) {
        spec.setParam("live_color", SimParam(32));  // зелёный
    }
}

// ---------------------------------------------------------------------------
// Мутация параметров
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::mutateSpec(const SimulationSpec& base, float amount) {
    SimulationSpec out = base;
    std::uniform_real_distribution<float> ud(-amount, amount);

    for (auto& [k, v] : out.params) {
        if (std::holds_alternative<float>(v)) {
            float cur = std::get<float>(v);
            float mut = cur * (1.0f + ud(rng_));
            v = SimParam(std::max(0.0001f, mut));
        } else if (std::holds_alternative<int>(v)) {
            int cur = std::get<int>(v);
            int range = std::max(1, (int)(cur * amount));
            std::uniform_int_distribution<int> id(-range, range);
            v = SimParam(std::max(1, cur + id(rng_)));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Случайное CA правило
// ---------------------------------------------------------------------------
void WorldInventor::randomizeCARule(CARule& rule, int seed) {
    std::mt19937 r(seed);
    rule.birth.clear(); rule.survive.clear();
    for (int i=0;i<=8;i++) {
        if (r()%3==0) rule.birth.push_back(i);
        if (r()%2==0) rule.survive.push_back(i);
    }
    if (rule.birth.empty()) rule.birth.push_back(3);
    if (rule.survive.empty()) { rule.survive.push_back(2); rule.survive.push_back(3); }
}

// ---------------------------------------------------------------------------
// Главный метод: invent()
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::invent(const InventionRequest& req) {
    rng_.seed(req.seed ? req.seed : (unsigned)time(nullptr));

    printf("\n[WorldInventor] Анализирую запрос: \"%s\"\n", req.userQuery.c_str());

    // Выбираем режим мышления (РСАД)
    int mode = rng_() % 3;
    if (req.forceNovel) mode = 2;  // Форсируем метафору для максимальной новизны

    // Также смотрим на ключевые слова для выбора режима
    auto has = [&](const char* s){ return req.userQuery.find(s) != std::string::npos; };
    if (has("похожее") || has("similar") || has("как") || has("like"))  mode=0;
    if (has("от") || has("from") || has("behavior") || has("поведени"))  mode=1;
    if (has("метафор") || has("природ") || has("nature") || has("новый")) mode=2;

    SimulationSpec result;
    switch(mode) {
        case 0:
            printf("[WorldInventor] Режим A: Комбинаторная аналогия\n");
            result = modeA_analogy(req);
            break;
        case 1:
            printf("[WorldInventor] Режим B: Обратный ход от поведения\n");
            result = modeB_backward(req);
            break;
        case 2:
            printf("[WorldInventor] Режим C: Природная метафора\n");
            result = modeC_metaphor(req);
            break;
    }

    printf("[WorldInventor] Создан мир: \"%s\" (новизна=%.2f)\n",
           result.title.c_str(), result.creativityScore);

    return result;
}

// ---------------------------------------------------------------------------
// Парсинг текстового запроса
// ---------------------------------------------------------------------------
SimulationSpec WorldInventor::parseUserQuery(const std::string& query) {
    InventionRequest req;
    req.userQuery = query;
    req.seed = (unsigned)std::hash<std::string>{}(query);

    // Проверяем пресеты напрямую
    for (auto& p : presetNames_) {
        if (query.find(p) != std::string::npos)
            return preset(p);
    }

    // Иначе — изобретаем
    return invent(req);
}
