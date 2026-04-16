// =============================================================================
// LearningCurve.cpp — Реализация ASCII-кривой обучения
// =============================================================================
#include "LearningCurve.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <numeric>

// ---------------------------------------------------------------------------
// Скользящее среднее success rate
// ---------------------------------------------------------------------------
float LearningCurve::smooth(int idx, int window) const {
    int lo = std::max(0, idx - window);
    int hi = std::min((int)points_.size()-1, idx + window);
    float sum = 0; int cnt = 0;
    for (int i = lo; i <= hi; i++) { sum += points_[i].successRate; cnt++; }
    return cnt > 0 ? sum/cnt : 0.0f;
}

std::string LearningCurve::renderBar(float value, int width, char fill, char empty) const {
    int filled = (int)std::round(value * width);
    filled = std::max(0, std::min(width, filled));
    std::string bar(filled, fill);
    bar.append(width - filled, empty);
    return bar;
}

// ---------------------------------------------------------------------------
// ASCII-график 2D (success rate и avg quality по эпизодам)
// ---------------------------------------------------------------------------
void LearningCurve::print(int W, int H) const {
    if (points_.empty()) { printf("[LearningCurve] No data\n"); return; }

    int N = (int)points_.size();

    printf("\n");
    printf("  Кривая обучения РСАД  (SR=success rate  Q=avg quality)\n");
    printf("  %-4s  ", "Ep");
    for (int i = 0; i < W; i++) putchar('─');
    printf("\n");

    // Построчно (H строк = 0..1 по оси Y)
    for (int row = H-1; row >= 0; row--) {
        float yLo = (float)row / H;
        float yHi = (float)(row+1) / H;
        float yMid = (yLo + yHi) * 0.5f;

        printf("  %4.0f%%│", yMid * 100);

        // Для каждой позиции X (эпизод) — какой символ?
        for (int col = 0; col < W; col++) {
            // Соответствующий индекс в данных
            int dataIdx = (int)((float)col / W * N);
            dataIdx = std::min(dataIdx, N-1);

            float sr = smooth(dataIdx, 2);
            float q  = points_[dataIdx].avgQuality;

            bool inSR = (sr >= yLo && sr < yHi);
            bool inQ  = (q  >= yLo && q  < yHi);

            if (inSR && inQ) putchar('*');   // обе кривые
            else if (inSR)   putchar('#');   // success rate
            else if (inQ)    putchar('.');   // quality
            else             putchar(' ');
        }
        printf("│\n");
    }

    printf("      └");
    for (int i = 0; i < W; i++) putchar('─');
    printf("┘\n");

    // Ось X
    printf("      0");
    int mid = N/2, last = N-1;
    int xMid = (int)((float)(W-1) * mid / (N-1));
    int xEnd = W-1;
    printf("%*sep%d%*sep%d\n",
           xMid, "", mid,
           xEnd - xMid - (int)std::to_string(mid).size() - 2, "",
           last);

    printf("  Легенда: # = success rate  . = avg quality  * = оба\n");
    printf("  Итог: SR=%.1f%%  AvgQ=%.3f  BestQ=%.3f  Edges=%d\n\n",
           points_.back().successRate*100,
           points_.back().avgQuality,
           [&]{float b=0; for(auto& p:points_) b=std::max(b,p.avgQuality); return b;}(),
           points_.back().memoryEdges);
}

// ---------------------------------------------------------------------------
// Таблица прогресса
// ---------------------------------------------------------------------------
void LearningCurve::printTable(int stride) const {
    if (points_.empty()) return;

    printf("\n┌──────┬────────┬─────────┬──────┬────────────────────┬───────┐\n");
    printf("│  Ep  │   SR   │  AvgQ   │ KEff │  Прогресс SR       │ Edges │\n");
    printf("├──────┼────────┼─────────┼──────┼────────────────────┼───────┤\n");

    for (int i = 0; i < (int)points_.size(); i += stride) {
        auto& p = points_[i];
        std::string bar = renderBar(p.successRate, 18, '#', '-');
        printf("│ %4d │ %5.1f%% │  %.3f  │ %4.1f │ %s │ %5d │\n",
               p.episode + 1,
               p.successRate * 100,
               p.avgQuality,
               p.kEff,
               bar.c_str(),
               p.memoryEdges);
    }
    // Последний эпизод (если не попал)
    if ((int)points_.size() % stride != 1) {
        auto& p = points_.back();
        std::string bar = renderBar(p.successRate, 18, '#', '-');
        printf("│ %4d │ %5.1f%% │  %.3f  │ %4.1f │ %s │ %5d │\n",
               p.episode + 1,
               p.successRate * 100,
               p.avgQuality,
               p.kEff,
               bar.c_str(),
               p.memoryEdges);
    }
    printf("└──────┴────────┴─────────┴──────┴────────────────────┴───────┘\n\n");
}

// ---------------------------------------------------------------------------
// CSV экспорт
// ---------------------------------------------------------------------------
void LearningCurve::saveCSV(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return;
    f << "episode,success_rate,avg_quality,k_eff,memory_edges\n";
    for (auto& p : points_)
        f << p.episode << "," << p.successRate << "," << p.avgQuality
          << "," << p.kEff << "," << p.memoryEdges << "\n";
    printf("[LearningCurve] Saved to %s\n", path.c_str());
}
