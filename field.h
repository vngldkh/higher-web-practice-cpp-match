#pragma once

#include <QColor>
#include <QPainter>
#include <QPointF>
#include <QRectF>

#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

class IGameObject {
public:
    virtual ~IGameObject() = default;

    virtual void update(double dt) = 0;
    virtual void render(QPainter& painter, const QRectF& boardRect, double cellSize) = 0;
    virtual int row() const = 0;
    virtual int column() const = 0;
    virtual int colorIndex() const = 0;
    virtual bool isBomb() const = 0;
    virtual bool contains(const QPointF& point, const QRectF& boardRect, double cellSize) const = 0;
    virtual bool isAnimating() const = 0;
    virtual bool isDestroying() const = 0;
    virtual bool isDestroyed() const = 0;
    virtual void moveTo(int row, int column) = 0;
    virtual void destroy() = 0;
};

template <std::size_t Capacity, std::size_t SlotSize, std::size_t SlotAlignment>
class ObjectPool {
public:
    ObjectPool() = default;
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ~ObjectPool() {
        for (std::size_t i = 0; i < Capacity; ++i) {
            if (used_[i]) {
                destroySlot(i);
            }
        }
    }

    template <typename T, typename... Args>
    T* create(Args&&... args) {
        static_assert(sizeof(T) <= SlotSize, "ObjectPool slot is too small for this type");
        static_assert(alignof(T) <= SlotAlignment, "ObjectPool slot alignment is too small for this type");

        for (std::size_t i = 0; i < Capacity; ++i) {
            if (!used_[i]) {
                used_[i] = true;
                destructors_[i] = [](void* object) {
                    static_cast<T*>(object)->~T();
                };

                try {
                    return new (&storage_[i]) T(std::forward<Args>(args)...);
                } catch (...) {
                    used_[i] = false;
                    destructors_[i] = nullptr;
                    throw;
                }
            }
        }

        return nullptr;
    }

    template <typename T>
    void destroy(T* object) {
        if (object == nullptr) {
            return;
        }

        for (std::size_t i = 0; i < Capacity; ++i) {
            const auto slotBegin = reinterpret_cast<std::uintptr_t>(&storage_[i]);
            const auto slotEnd = slotBegin + SlotSize;
            const auto objectAddress = reinterpret_cast<std::uintptr_t>(object);

            if (objectAddress >= slotBegin && objectAddress < slotEnd) {
                destroySlot(i);
                used_[i] = false;
                destructors_[i] = nullptr;
                return;
            }
        }
    }

private:
    using Storage = typename std::aligned_storage<SlotSize, SlotAlignment>::type;
    using Destructor = void (*)(void*);

    void destroySlot(std::size_t index) {
        if (destructors_[index] != nullptr) {
            destructors_[index](&storage_[index]);
        }
    }

    std::array<Storage, Capacity> storage_{};
    std::array<bool, Capacity> used_{};
    std::array<Destructor, Capacity> destructors_{};
};

class MatchObject final : public IGameObject {
public:
    MatchObject(int row, int column, int colorIndex, bool bomb);

    void update(double dt) override;
    void render(QPainter& painter, const QRectF& boardRect, double cellSize) override;
    int row() const override;
    int column() const override;
    int colorIndex() const override;
    bool isBomb() const override;
    bool contains(const QPointF& point, const QRectF& boardRect, double cellSize) const override;
    bool isAnimating() const override;
    bool isDestroying() const override;
    bool isDestroyed() const override;
    void moveTo(int row, int column) override;
    void destroy() override;

private:
    static constexpr double ObjectPadding = 0.16;
    static const std::array<QColor, 6> Palette;

    template <typename... Visitors>
    struct Overloaded : Visitors... {
        using Visitors::operator()...;
    };

    template <typename... Visitors>
    static Overloaded<std::decay_t<Visitors>...> overloaded(Visitors&&... visitors) {
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

    static double smoothStep(double value);
    QPointF center(const QRectF& boardRect, double cellSize) const;
    QColor color() const;

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
    static constexpr int Size = 10;
    static constexpr int CellCount = Size * Size;
    static constexpr int ColorCount = 6;

    Field();

    void update(double dt);
    void render(QPainter& painter);
    void click(int x, int y);

private:
    static constexpr double BombChance = 0.01;

    using CellArray = std::array<IGameObject*, CellCount>;
    using MarkedCells = std::array<bool, CellCount>;

    int index(int row, int column) const;
    IGameObject* cell(int row, int column) const;
    void setCell(int row, int column, IGameObject* object);
    IGameObject* createObject(int row, int column);
    int randomColorFor(int row, int column) const;
    bool colorCreatesMatch(int row, int column, int colorIndex) const;
    bool hasAnimations() const;
    void collectDestroyed();
    bool collapseAndFill();
    bool activateMatches();
    void activateObject(IGameObject* object, MarkedCells& marked);
    void activateCell(int row, int column, MarkedCells& marked);
    void activateBombColor(int colorIndex, MarkedCells& marked);
    void startDestruction(const MarkedCells& marked);

    ObjectPool<CellCount, sizeof(MatchObject), alignof(MatchObject)> objectPool_;
    CellArray cells_{};
    QRectF boardRect_;
    double cellSize_ = 0.0;
};
