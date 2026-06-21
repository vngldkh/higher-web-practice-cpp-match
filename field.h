#pragma once

#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QRectF>

#include <array>
#include <cstddef>
#include <new>
#include <queue>
#include <type_traits>
#include <utility>
#include <variant>

class IGameObject {
public:
    virtual ~IGameObject() = default;

    virtual void Update(double dt) = 0;
    virtual void Render(QPainter& painter, const QRectF& boardRect, double cellSize) = 0;
    virtual int GetRow() const = 0;
    virtual int GetColumn() const = 0;
    virtual int GetColorIndex() const = 0;
    virtual bool IsBomb() const = 0;
    virtual bool Contains(const QPointF& point, const QRectF& boardRect, double cellSize) const = 0;
    virtual bool IsAnimating() const = 0;
    virtual bool IsDestroyed() const = 0;
    virtual void MoveTo(int row, int column) = 0;
    virtual void Destroy() = 0;
};

template <typename T, std::size_t Capacity>
class ObjectPool {
public:
    ObjectPool() = default;
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ~ObjectPool() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (used_[i]) {
                DestroySlot(i);
            }
        }
    }

    template <typename... Args>
    T* Create(Args&&... args) {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!used_[i]) {
                used_[i] = true;
                return new (&storage_[i]) T(std::forward<Args>(args)...);
            }
        }

        return nullptr;
    }

    void Destroy(T* object) {
        if (object == nullptr) {
            return;
        }

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (object == reinterpret_cast<T*>(&storage_[i])) {
                object->~T();
                used_[i] = false;
                return;
            }
        }
    }

private:
    using Storage = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

    void DestroySlot(std::size_t index) {
        reinterpret_cast<T*>(&storage_[index])->~T();
    }

    std::array<Storage, Capacity> storage_{};
    std::array<bool, Capacity> used_{};
};

class MatchObject final : public IGameObject {
public:
    MatchObject(int row, int column, int colorIndex, bool bomb);

    void Update(double dt) override;
    void Render(QPainter& painter, const QRectF& boardRect, double cellSize) override;
    int GetRow() const override;
    int GetColumn() const override;
    int GetColorIndex() const override;
    bool IsBomb() const override;
    bool Contains(const QPointF& point, const QRectF& boardRect, double cellSize) const override;
    bool IsAnimating() const override;
    bool IsDestroyed() const override;
    void MoveTo(int row, int column) override;
    void Destroy() override;

private:
    static constexpr double ObjectPadding = 0.16;
    static const std::array<QColor, 6> Palette;

    template <typename... Visitors>
    struct Overloaded : Visitors... {
        using Visitors::operator()...;
    };

    template <typename... Visitors>
    static Overloaded<std::decay_t<Visitors>...> MakeOverloaded(Visitors&&... visitors) {
        return {std::forward<Visitors>(visitors)...};
    }

    struct Appearing {
        double elapsed = 0.0;
        double duration = 0.22;
    };

    struct Idle {
    };

    struct Falling {
        double fromRow = 0.0;
        double toRow = 0.0;
        double elapsed = 0.0;
        double duration = 0.18;
    };

    struct Destroying {
        double elapsed = 0.0;
        double duration = 0.18;
    };

    using State = std::variant<Appearing, Idle, Falling, Destroying>;

    static double SmoothStep(double value);
    QPointF GetCenter(const QRectF& boardRect, double cellSize) const;
    QColor GetColor() const;

    int row_ = 0;
    int column_ = 0;
    int colorIndex_ = 0;
    bool bomb_ = false;
    bool destroyed_ = false;
    double visualRow_ = 0.0;
    double blinkTime_ = 0.0;
    State state_ = Appearing{};
};

class Field {
public:
    Field();

    void Update(double dt);
    void Render(QPainter& painter);
    void Click(int x, int y);

private:
    static constexpr int kSize = 10;
    static constexpr int kCellCount = kSize * kSize;
    static constexpr int kColorCount = 6;
    static constexpr double kBombChance = 0.01;

    using CellArray = std::array<IGameObject*, kCellCount>;
    using MarkedCells = std::array<bool, kCellCount>;
    using CellPosition = std::pair<int, int>;

    int GetIndex(int row, int column) const;
    bool IsInside(int row, int column) const;
    const IGameObject* GetCell(int row, int column) const;
    IGameObject* GetCell(int row, int column);
    void SetCell(int row, int column, IGameObject* object);
    void CreateObject(int row, int column);
    int GetRandomColorFor(int row, int column) const;
    bool DoesColorCreateMatch(int row, int column, int colorIndex) const;
    bool HasAnimations() const;
    void CollectDestroyed();
    bool CollapseAndFill();
    bool ActivateMatches();
    void ActivateObject(IGameObject* object, MarkedCells* marked);
    void ActivateConnectedColor(int row, int column, int colorIndex, MarkedCells* marked);
    void ActivateBombColor(int colorIndex, MarkedCells* marked);
    void StartDestruction(const MarkedCells& marked);

    ObjectPool<MatchObject, kCellCount> objectPool_;
    CellArray cells_{};
    QRectF boardRect_;
    double cellSize_ = 0.0;
};
