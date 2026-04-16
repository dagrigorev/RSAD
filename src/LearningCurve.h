#pragma once
// =============================================================================
// LearningCurve.h — ASCII-визуализация кривой обучения
// =============================================================================
#include <vector>
#include <string>

struct CurvePoint {
    int   episode;
    float successRate;   // 0..1
    float avgQuality;    // 0..1
    float kEff;
    int   memoryEdges;
};

class LearningCurve {
public:
    void addPoint(const CurvePoint& p) { points_.push_back(p); }
    void clear() { points_.clear(); }

    /// Вывести ASCII-график в консоль
    void print(int width = 60, int height = 12) const;

    /// Краткая таблица с итогами каждые N эпизодов
    void printTable(int stride = 5) const;

    /// Экспорт в CSV
    void saveCSV(const std::string& path) const;

    const std::vector<CurvePoint>& getPoints() const { return points_; }

private:
    std::vector<CurvePoint> points_;

    // Helpers
    std::string renderBar(float value, int width, char fill='█', char empty='░') const;
    float smooth(int idx, int window = 3) const;  // скользящее среднее SR
};
