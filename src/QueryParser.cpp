// =============================================================================
// QueryParser.cpp — Реализация NLP парсера запросов
// =============================================================================
#include "QueryParser.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <regex>
#include <sstream>
#include <cmath>

// ---------------------------------------------------------------------------
// Конструктор
// ---------------------------------------------------------------------------
QueryParser::QueryParser() { initKeywords(); }

// ---------------------------------------------------------------------------
// Словари intent-ключевых слов
// ---------------------------------------------------------------------------
void QueryParser::initKeywords() {
    intentKeywords_["SOLVE"] = {
        {"найди",1.0f},{"найти",1.0f},{"путь",0.8f},{"маршрут",0.8f},
        {"реши",1.0f},{"решить",1.0f},{"поиск",0.7f},{"пройди",0.9f},
        {"find",1.0f},{"path",0.8f},{"solve",1.0f},{"navigate",0.9f},
        {"route",0.8f},{"go",0.6f},{"reach",0.7f},{"move",0.6f},
        {"лабиринт",0.7f},{"maze",0.7f},{"сетка",0.5f},{"grid",0.5f},
        {"от",0.3f},{"до",0.3f},{"from",0.3f},{"to",0.3f},
    };
    intentKeywords_["TRAIN"] = {
        {"обучи",1.0f},{"обучить",1.0f},{"тренируй",1.0f},{"тренировка",1.0f},
        {"train",1.0f},{"training",1.0f},{"learn",0.9f},{"обучение",1.0f},
        {"эпизод",0.8f},{"episode",0.8f},{"итерац",0.7f},{"iteration",0.7f},
        {"улучши",0.7f},{"improve",0.7f},
    };
    intentKeywords_["STATS"] = {
        {"статистика",1.0f},{"статистику",1.0f},{"stats",1.0f},{"results",0.8f},
        {"покажи",0.5f},{"show",0.5f},{"память",0.7f},{"memory",0.7f},
        {"история",0.8f},{"history",0.8f},
    };
    intentKeywords_["HELP"] = {
        {"помощь",1.0f},{"помоги",0.9f},{"help",1.0f},{"?",0.5f},
        {"как",0.4f},{"what",0.4f},{"how",0.4f},{"команды",0.8f},
        {"commands",0.8f},
    };
}

// ---------------------------------------------------------------------------
// Нормализация: нижний регистр, убираем лишние символы
// ---------------------------------------------------------------------------
std::string QueryParser::normalize(const std::string& s) const {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        // Оставляем: буквы (ASCII + UTF-8 кириллица), цифры, пробел, - . , ( ) %
        if (c == '\n' || c == '\t' || c == '\r') { out += ' '; continue; }
        if (c < 128) out += (char)tolower(c);
        else out += (char)c;  // UTF-8 байт оставляем как есть (кириллица)
    }
    // Схлопываем несколько пробелов в один
    std::string res;
    bool space = false;
    for (char c : out) {
        if (c == ' ') { if (!space) { res += c; space = true; } }
        else { res += c; space = false; }
    }
    return res;
}

std::vector<std::string> QueryParser::tokenize(const std::string& s) const {
    std::vector<std::string> tokens;
    std::istringstream ss(s);
    std::string tok;
    while (ss >> tok) {
        // Убираем знаки препинания в начале/конце токена
        while (!tok.empty() && (tok.front()==',' || tok.front()=='.' ||
                                 tok.front()=='(' || tok.front()==')'))
            tok.erase(tok.begin());
        while (!tok.empty() && (tok.back()==',' || tok.back()=='.' ||
                                 tok.back()=='(' || tok.back()==')'))
            tok.pop_back();
        if (!tok.empty()) tokens.push_back(tok);
    }
    return tokens;
}

// ---------------------------------------------------------------------------
// Вспомогательные
// ---------------------------------------------------------------------------
bool QueryParser::containsAny(const std::string& s,
                                const std::vector<std::string>& words) const {
    for (auto& w : words) if (s.find(w) != std::string::npos) return true;
    return false;
}

std::optional<int> QueryParser::extractIntAfter(const std::string& s,
                                                   const std::string& marker) const {
    auto pos = s.find(marker);
    if (pos == std::string::npos) return std::nullopt;
    pos += marker.size();
    while (pos < s.size() && (s[pos]==' '||s[pos]=='=')) pos++;
    if (pos >= s.size() || !isdigit((unsigned char)s[pos])) return std::nullopt;
    int val = 0;
    while (pos < s.size() && isdigit((unsigned char)s[pos]))
        val = val*10 + (s[pos++]-'0');
    return val;
}

std::optional<float> QueryParser::extractFloatAfter(const std::string& s,
                                                       const std::string& marker) const {
    auto pos = s.find(marker);
    if (pos == std::string::npos) return std::nullopt;
    pos += marker.size();
    while (pos < s.size() && (s[pos]==' '||s[pos]=='=')) pos++;
    if (pos >= s.size() || (!isdigit((unsigned char)s[pos]) && s[pos]!='.'))
        return std::nullopt;
    float val = 0;
    bool decimal = false; float factor = 0.1f;
    while (pos < s.size() && (isdigit((unsigned char)s[pos]) || s[pos]=='.')) {
        if (s[pos]=='.') { decimal=true; pos++; continue; }
        if (!decimal) val = val*10 + (s[pos]-'0');
        else { val += (s[pos]-'0')*factor; factor*=0.1f; }
        pos++;
    }
    return val;
}

// ---------------------------------------------------------------------------
// Извлечение размера сетки
// ---------------------------------------------------------------------------
void QueryParser::extractGridSize(const std::string& norm,
                                   const std::vector<std::string>& tokens,
                                   QuerySlots& out) const {
    // Паттерн "NxN" или "N x N" или "N на N"
    std::regex rxNN(R"((\d+)\s*[xхXнa]\s*(\d+))");  // x, х(кир), X, н, a
    std::smatch m;
    if (std::regex_search(norm, m, rxNN)) {
        int a = std::stoi(m[1].str());
        int b = std::stoi(m[2].str());
        if (a == b && a >= 3 && a <= 50) out.gridSize = a;
        return;
    }
    // Паттерн "сетка 10" / "grid 10" / "size 10"
    for (auto& marker : {"сетк","grid","size","размер","field","поле"}) {
        auto v = extractIntAfter(norm, marker);
        if (v && *v >= 3 && *v <= 50) { out.gridSize = v; return; }
    }
    // Одиночное число 5–30 рядом с ключевым словом
    for (auto& tok : tokens) {
        if (tok.size() >= 1 && tok.size() <= 2) {
            bool allDigit = true;
            for (char c : tok) if (!isdigit((unsigned char)c)) {allDigit=false;break;}
            if (allDigit) {
                int v = std::stoi(tok);
                if (v >= 5 && v <= 30 && !out.gridSize) out.gridSize = v;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Извлечение координат: (x,y) или x,y или "от x y до x y"
// ---------------------------------------------------------------------------
void QueryParser::extractCoords(const std::string& norm, QuerySlots& out) const {
    // Паттерн (x,y)
    std::regex rxPair(R"(\(\s*(\d+)\s*,\s*(\d+)\s*\))");
    std::vector<std::pair<int,int>> pairs;
    auto begin = std::sregex_iterator(norm.begin(), norm.end(), rxPair);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        pairs.push_back({std::stoi((*it)[1].str()), std::stoi((*it)[2].str())});
    }
    if (pairs.size() >= 2) {
        out.startX = pairs[0].first;  out.startY = pairs[0].second;
        out.goalX  = pairs[1].first;  out.goalY  = pairs[1].second;
        return;
    }
    if (pairs.size() == 1) {
        out.goalX = pairs[0].first; out.goalY = pairs[0].second;
        return;
    }

    // "старт x y" / "start x y"
    std::regex rxStart(R"((?:старт|start|begin|от|from)\s+(\d+)\s+(\d+))");
    std::smatch ms;
    if (std::regex_search(norm, ms, rxStart)) {
        out.startX = std::stoi(ms[1].str());
        out.startY = std::stoi(ms[2].str());
    }
    // "цель x y" / "goal x y"
    std::regex rxGoal(R"((?:цел|goal|end|до|to)\s+(\d+)\s+(\d+))");
    std::smatch mg;
    if (std::regex_search(norm, mg, rxGoal)) {
        out.goalX = std::stoi(mg[1].str());
        out.goalY = std::stoi(mg[2].str());
    }

    // Текстовые описания позиций
    if (containsAny(norm,{"top-left","левый верх","верхний левый","начало","угол"}))
        { out.startX=0; out.startY=0; }
    if (containsAny(norm,{"bottom-right","правый низ","нижний правый","конец","противоположный"}))
        { /* goalX/Y вычислятся из gridSize */ }
}

// ---------------------------------------------------------------------------
// Плотность препятствий
// ---------------------------------------------------------------------------
void QueryParser::extractDensity(const std::string& norm, QuerySlots& out) const {
    // "20%" / "20 процентов" / "density 0.2"
    std::regex rxPct(R"((\d+)\s*%?)");
    // Ищем рядом с ключевыми словами
    for (auto& kw : {"препятстви","obstacles","density","плотност","стен","walls"}) {
        auto pos = norm.find(kw);
        if (pos == std::string::npos) continue;
        // Сканируем ±20 символов
        int lo = (int)pos - 15, hi = (int)pos + (int)strlen(kw) + 15;
        lo = std::max(lo, 0); hi = std::min(hi, (int)norm.size());
        std::string sub = norm.substr(lo, hi-lo);
        // Ищем число в подстроке
        std::smatch m;
        if (std::regex_search(sub, m, std::regex(R"((\d+\.?\d*)\s*%?)"))) {
            float v = std::stof(m[1].str());
            if (v > 1.0f) v /= 100.0f;  // 20% → 0.2
            if (v >= 0.0f && v <= 0.8f) { out.obstacleDensity = v; return; }
        }
    }
    // Просто "N%" в тексте
    std::regex rxPct2(R"((\d+)\s*%)");
    std::smatch mp;
    if (std::regex_search(norm, mp, rxPct2)) {
        float v = std::stof(mp[1].str()) / 100.0f;
        if (v >= 0.0f && v <= 0.8f) out.obstacleDensity = v;
    }
}

// ---------------------------------------------------------------------------
// Сложность
// ---------------------------------------------------------------------------
void QueryParser::extractDifficulty(const std::string& norm, QuerySlots& out) const {
    if (containsAny(norm,{"простой","простая","легкий","легкая","easy","simple","начальный"}))
        out.difficultyTag = "easy";
    else if (containsAny(norm,{"средний","средняя","medium","normal","стандарт"}))
        out.difficultyTag = "medium";
    else if (containsAny(norm,{"сложный","сложная","hard","difficult","труд"}))
        out.difficultyTag = "hard";
    else if (containsAny(norm,{"экстрем","extreme","очень сложн","impossible","невозможн"}))
        out.difficultyTag = "extreme";
}

// ---------------------------------------------------------------------------
// Intent classification (keyword voting)
// ---------------------------------------------------------------------------
void QueryParser::extractIntent(const std::string& norm,
                                 const std::vector<std::string>& tokens,
                                 QuerySlots& out) const {
    std::unordered_map<std::string, float> scores;
    scores["SOLVE"] = 0; scores["TRAIN"] = 0;
    scores["STATS"] = 0; scores["HELP"]  = 0;

    for (auto& [intent, keywords] : intentKeywords_) {
        for (auto& kw : keywords) {
            if (norm.find(kw.word) != std::string::npos)
                scores[intent] += kw.weight;
        }
    }

    float best = -1; std::string bestIntent = "SOLVE";
    for (auto& [k,v] : scores) if (v > best) { best=v; bestIntent=k; }

    out.confidence = best / 5.0f;  // нормализуем грубо

    if (bestIntent == "TRAIN")  { out.intent = QueryIntent::TRAIN; }
    else if (bestIntent=="STATS"){ out.intent = QueryIntent::SHOW_STATS; }
    else if (bestIntent=="HELP") { out.intent = QueryIntent::HELP; }
    else                         { out.intent = QueryIntent::SOLVE_PATHFINDING; }

    // Извлекаем число эпизодов для TRAIN
    if (out.intent == QueryIntent::TRAIN) {
        for (auto& kw : {"эпизод","episode","итераци","iteration","раз","times"}) {
            auto v = extractIntAfter(norm, kw);
            if (v && *v >= 1 && *v <= 10000) { out.trainEpisodes = v; break; }
        }
        // Или просто число 10..1000
        for (auto& tok : tokens) {
            if (tok.size()>=2 && tok.size()<=5) {
                bool allD=true;
                for (char c:tok) if(!isdigit((unsigned char)c)){allD=false;break;}
                if (allD) { int v=std::stoi(tok); if(v>=5&&v<=5000){out.trainEpisodes=v;break;} }
            }
        }
        if (!out.trainEpisodes) out.trainEpisodes = 50;
    }

    // Seed
    auto sv = extractIntAfter(norm, "seed");
    if (!sv) sv = extractIntAfter(norm, "сид");
    if (sv) out.seed = sv;
}

// ---------------------------------------------------------------------------
// Применение defaults по сложности
// ---------------------------------------------------------------------------
void QueryParser::applyDifficultyDefaults(QuerySlots& out) const {
    if (out.difficultyTag == "easy") {
        if (!out.gridSize)        out.gridSize        = 6;
        if (!out.obstacleDensity) out.obstacleDensity = 0.10f;
    } else if (out.difficultyTag == "medium") {
        if (!out.gridSize)        out.gridSize        = 10;
        if (!out.obstacleDensity) out.obstacleDensity = 0.20f;
    } else if (out.difficultyTag == "hard") {
        if (!out.gridSize)        out.gridSize        = 12;
        if (!out.obstacleDensity) out.obstacleDensity = 0.30f;
    } else if (out.difficultyTag == "extreme") {
        if (!out.gridSize)        out.gridSize        = 15;
        if (!out.obstacleDensity) out.obstacleDensity = 0.38f;
    }
}

// ---------------------------------------------------------------------------
// Формирование Problem из слотов
// ---------------------------------------------------------------------------
Problem QueryParser::buildProblem(const QuerySlots& slots) const {
    Problem p;
    int G = slots.gridSize.value_or(RSADConfig::GRID_SIZE);
    G = std::max(3, std::min(50, G));
    p.init(G);

    p.startX = slots.startX.value_or(0);
    p.startY = slots.startY.value_or(0);
    p.goalX  = slots.goalX.value_or(G-1);
    p.goalY  = slots.goalY.value_or(G-1);

    // Зажимаем в границы
    auto clamp = [G](int v){ return std::max(0, std::min(G-1, v)); };
    p.startX=clamp(p.startX); p.startY=clamp(p.startY);
    p.goalX =clamp(p.goalX);  p.goalY =clamp(p.goalY);

    float density = slots.obstacleDensity.value_or(0.20f);
    unsigned seed = slots.seed.value_or(42);
    p.randomObstacles(density, seed);

    return p;
}

// ---------------------------------------------------------------------------
// Объяснение для пользователя
// ---------------------------------------------------------------------------
std::string QueryParser::explain(const QuerySlots& slots) const {
    std::ostringstream ss;
    ss << "Понял: ";

    switch(slots.intent) {
        case QueryIntent::SOLVE_PATHFINDING:
            ss << "поиск пути";
            if (slots.gridSize) ss << " в сетке " << *slots.gridSize << "×" << *slots.gridSize;
            if (slots.startX)   ss << ", старт (" << *slots.startX << "," << *slots.startY << ")";
            if (slots.goalX)    ss << ", цель (" << *slots.goalX << "," << *slots.goalY << ")";
            if (slots.obstacleDensity)
                ss << ", препятствия " << (int)(*slots.obstacleDensity*100) << "%";
            if (!slots.difficultyTag.empty()) ss << " [" << slots.difficultyTag << "]";
            break;
        case QueryIntent::TRAIN:
            ss << "обучение РСАД, эпизодов: " << slots.trainEpisodes.value_or(50);
            break;
        case QueryIntent::SHOW_STATS:
            ss << "показать статистику";
            break;
        case QueryIntent::HELP:
            ss << "помощь";
            break;
        default:
            ss << "неизвестный запрос";
    }

    if (slots.confidence < 0.2f) ss << " [низкая уверенность: " << (int)(slots.confidence*100) << "%]";
    return ss.str();
}

// ---------------------------------------------------------------------------
// Главный метод parse()
// ---------------------------------------------------------------------------
ParseResult QueryParser::parse(const std::string& query) const {
    ParseResult result;
    result.slots.rawQuery = query;

    if (query.empty()) {
        result.errorMsg = "Пустой запрос";
        return result;
    }

    std::string norm = normalize(query);
    auto tokens = tokenize(norm);

    // Заполняем слоты
    extractIntent   (norm, tokens, result.slots);
    extractDifficulty(norm, result.slots);
    applyDifficultyDefaults(result.slots);
    extractGridSize (norm, tokens, result.slots);
    extractCoords   (norm, result.slots);
    extractDensity  (norm, result.slots);

    // Для TRAIN/STATS/HELP не строим Problem
    if (result.slots.intent == QueryIntent::TRAIN    ||
        result.slots.intent == QueryIntent::SHOW_STATS||
        result.slots.intent == QueryIntent::HELP) {
        result.valid = true;
        result.slots.explanation = explain(result.slots);
        return result;
    }

    result.problem = buildProblem(result.slots);
    result.slots.explanation = explain(result.slots);
    result.valid = true;
    return result;
}
