#include "field.h"

#include <QRandomGenerator>

#include <algorithm>
#include <cmath>

const std::array<QColor, 6> MatchObject::Palette = {
    QColor(231, 76, 60),
    QColor(46, 204, 113),
    QColor(52, 152, 219),
    QColor(241, 196, 15),
    QColor(155, 89, 182),
    QColor(26, 188, 156),
};

double MatchObject::smoothStep(double value) {
    value = std::clamp(value, 0.0, 1.0);
    return value * value * (3.0 - 2.0 * value);
}

MatchObject::MatchObject(int row, int column, int colorIndex, bool bomb)
    : row_(row)
    , column_(column)
    , colorIndex_(colorIndex)
    , bomb_(bomb)
    , visualRow_(row) {
}

void MatchObject::update(double dt) {
    blinkTime_ += dt;

    std::visit(overloaded(
        [this, dt](Appearing& state) {
            state.elapsed += dt;
            if (state.elapsed >= state.duration) {
                state_ = Idle{};
            }
        },
        [](Idle&) {
        },
        [this, dt](Falling& state) {
            state.elapsed += dt;
            const double progress = smoothStep(state.elapsed / state.duration);
            visualRow_ = state.fromRow + (state.toRow - state.fromRow) * progress;
            if (state.elapsed >= state.duration) {
                visualRow_ = state.toRow;
                state_ = Idle{};
            }
        },
        [this, dt](Destroying& state) {
            state.elapsed += dt;
            if (state.elapsed >= state.duration) {
                destroyed_ = true;
            }
        }
    ), state_);
}

void MatchObject::render(QPainter& painter, const QRectF& boardRect, double cellSize) {
    if (destroyed_) {
        return;
    }

    const QPointF objectCenter = center(boardRect, cellSize);
    const double objectScale = std::visit(overloaded(
        [](const Appearing& state) -> double {
            return smoothStep(state.elapsed / state.duration);
        },
        [](const Idle&) -> double {
            return 1.0;
        },
        [](const Falling&) -> double {
            return 1.0;
        },
        [](const Destroying& state) -> double {
            return 1.0 - smoothStep(state.elapsed / state.duration);
        }
    ), state_);
    const double radius = cellSize * (0.5 - ObjectPadding) * objectScale;
    if (radius <= 0.0) {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(color());
    painter.drawEllipse(objectCenter, radius, radius);
}

int MatchObject::row() const {
    return row_;
}

int MatchObject::column() const {
    return column_;
}

int MatchObject::colorIndex() const {
    return colorIndex_;
}

bool MatchObject::isBomb() const {
    return bomb_;
}

bool MatchObject::contains(const QPointF& point, const QRectF& boardRect, double cellSize) const {
    if (destroyed_ || isAnimating()) {
        return false;
    }

    const QPointF objectCenter = center(boardRect, cellSize);
    const double radius = cellSize * (0.5 - ObjectPadding);
    const double dx = point.x() - objectCenter.x();
    const double dy = point.y() - objectCenter.y();

    return dx * dx + dy * dy <= radius * radius;
}

bool MatchObject::isAnimating() const {
    return !std::holds_alternative<Idle>(state_);
}

bool MatchObject::isDestroyed() const {
    return destroyed_;
}

void MatchObject::moveTo(int row, int column) {
    if (row_ == row && column_ == column) {
        return;
    }

    const double distance = std::abs(row - row_);
    state_ = Falling{visualRow_, static_cast<double>(row), 0.0, 0.13 + distance * 0.055};
    row_ = row;
    column_ = column;
}

void MatchObject::destroy() {
    if (std::holds_alternative<Destroying>(state_) || destroyed_) {
        return;
    }

    state_ = Destroying{};
}

QColor MatchObject::color() const {
    QColor result = Palette[colorIndex_];
    if (!bomb_) {
        return result;
    }

    const double phase = (std::sin(blinkTime_ * 7.5) + 1.0) * 0.5;
    const QColor background(0, 0, 0);
    result.setRedF(result.redF() * phase + background.redF() * (1.0 - phase));
    result.setGreenF(result.greenF() * phase + background.greenF() * (1.0 - phase));
    result.setBlueF(result.blueF() * phase + background.blueF() * (1.0 - phase));

    return result;
}

QPointF MatchObject::center(const QRectF& boardRect, double cellSize) const {
    return QPointF(boardRect.left() + (column_ + 0.5) * cellSize,
                   boardRect.top() + (visualRow_ + 0.5) * cellSize);
}

Field::Field() {
    for (int row = 0; row < Size; ++row) {
        for (int column = 0; column < Size; ++column) {
            setCell(row, column, createObject(row, column));
        }
    }
}

void Field::update(double dt) {
    for (IGameObject* object : cells_) {
        if (object != nullptr) {
            object->update(dt);
        }
    }

    collectDestroyed();

    if (hasAnimations()) {
        return;
    }

    if (collapseAndFill()) {
        return;
    }

    activateMatches();
}

void Field::render(QPainter& painter) {
    const QRectF bounds = painter.viewport();
    const double boardSize = std::min(bounds.width(), bounds.height()) * 0.92;
    cellSize_ = boardSize / Size;
    boardRect_ = QRectF(bounds.center().x() - boardSize * 0.5,
                        bounds.center().y() - boardSize * 0.5,
                        boardSize,
                        boardSize);

    painter.save();
    painter.setPen(QPen(QColor(45, 45, 48), 1.0));
    painter.setBrush(QColor(15, 15, 18));
    painter.drawRect(boardRect_);

    for (int row = 1; row < Size; ++row) {
        const double y = boardRect_.top() + row * cellSize_;
        painter.drawLine(QPointF(boardRect_.left(), y), QPointF(boardRect_.right(), y));
    }

    for (int column = 1; column < Size; ++column) {
        const double x = boardRect_.left() + column * cellSize_;
        painter.drawLine(QPointF(x, boardRect_.top()), QPointF(x, boardRect_.bottom()));
    }

    for (IGameObject* object : cells_) {
        if (object != nullptr) {
            object->render(painter, boardRect_, cellSize_);
        }
    }

    painter.restore();
}

void Field::click(int x, int y) {
    if (cellSize_ <= 0.0 || hasAnimations()) {
        return;
    }

    const QPointF point(x, y);
    MarkedCells marked{};

    for (IGameObject* object : cells_) {
        if (object != nullptr && object->contains(point, boardRect_, cellSize_)) {
            activateObject(object, marked);
            startDestruction(marked);
            return;
        }
    }
}

int Field::index(int row, int column) const {
    return row * Size + column;
}

bool Field::inside(int row, int column) const {
    return row >= 0 && row < Size && column >= 0 && column < Size;
}

IGameObject* Field::cell(int row, int column) const {
    return cells_[index(row, column)];
}

void Field::setCell(int row, int column, IGameObject* object) {
    cells_[index(row, column)] = object;
}

IGameObject* Field::createObject(int row, int column) {
    const int color = randomColorFor(row, column);
    const bool bomb = QRandomGenerator::global()->generateDouble() < BombChance;
    return objectPool_.create(row, column, color, bomb);
}

int Field::randomColorFor(int row, int column) const {
    std::array<int, ColorCount> colors{};
    for (int i = 0; i < ColorCount; ++i) {
        colors[i] = i;
    }

    for (int i = ColorCount - 1; i > 0; --i) {
        const int swapIndex = QRandomGenerator::global()->bounded(i + 1);
        std::swap(colors[i], colors[swapIndex]);
    }

    for (int color : colors) {
        if (!colorCreatesMatch(row, column, color)) {
            return color;
        }
    }

    return colors.front();
}

bool Field::colorCreatesMatch(int row, int column, int colorIndex) const {
    int horizontal = 1;
    for (int c = column - 1; c >= 0 && cell(row, c) != nullptr && cell(row, c)->colorIndex() == colorIndex; --c) {
        ++horizontal;
    }
    for (int c = column + 1; c < Size && cell(row, c) != nullptr && cell(row, c)->colorIndex() == colorIndex; ++c) {
        ++horizontal;
    }

    int vertical = 1;
    for (int r = row - 1; r >= 0 && cell(r, column) != nullptr && cell(r, column)->colorIndex() == colorIndex; --r) {
        ++vertical;
    }
    for (int r = row + 1; r < Size && cell(r, column) != nullptr && cell(r, column)->colorIndex() == colorIndex; ++r) {
        ++vertical;
    }

    return horizontal >= 3 || vertical >= 3;
}

bool Field::hasAnimations() const {
    return std::any_of(cells_.begin(), cells_.end(), [](const IGameObject* object) {
        return object != nullptr && object->isAnimating();
    });
}

void Field::collectDestroyed() {
    for (IGameObject*& object : cells_) {
        if (object != nullptr && object->isDestroyed()) {
            objectPool_.destroy(static_cast<MatchObject*>(object));
            object = nullptr;
        }
    }
}

bool Field::collapseAndFill() {
    bool changed = false;

    for (int column = 0; column < Size; ++column) {
        int writeRow = Size - 1;

        for (int row = Size - 1; row >= 0; --row) {
            IGameObject* object = cell(row, column);
            if (object == nullptr) {
                continue;
            }

            setCell(row, column, nullptr);
            setCell(writeRow, column, object);
            object->moveTo(writeRow, column);
            changed = changed || row != writeRow;
            --writeRow;
        }

        for (int row = writeRow; row >= 0; --row) {
            IGameObject* object = createObject(row, column);
            setCell(row, column, object);
            changed = true;
        }
    }

    return changed;
}

bool Field::activateMatches() {
    MarkedCells matched{};

    for (int row = 0; row < Size; ++row) {
        int start = 0;
        while (start < Size) {
            IGameObject* object = cell(row, start);
            if (object == nullptr) {
                ++start;
                continue;
            }

            const int color = object->colorIndex();
            int end = start + 1;
            while (end < Size && cell(row, end) != nullptr && cell(row, end)->colorIndex() == color) {
                ++end;
            }

            if (end - start >= 3) {
                for (int column = start; column < end; ++column) {
                    activateConnectedColor(row, column, color, matched);
                }
            }

            start = end;
        }
    }

    for (int column = 0; column < Size; ++column) {
        int start = 0;
        while (start < Size) {
            IGameObject* object = cell(start, column);
            if (object == nullptr) {
                ++start;
                continue;
            }

            const int color = object->colorIndex();
            int end = start + 1;
            while (end < Size && cell(end, column) != nullptr && cell(end, column)->colorIndex() == color) {
                ++end;
            }

            if (end - start >= 3) {
                for (int row = start; row < end; ++row) {
                    activateConnectedColor(row, column, color, matched);
                }
            }

            start = end;
        }
    }

    const bool anyMatched = std::any_of(matched.begin(), matched.end(), [](bool value) {
        return value;
    });
    if (anyMatched) {
        startDestruction(matched);
    }

    return anyMatched;
}

void Field::activateObject(IGameObject* object, MarkedCells& marked) {
    if (object->isBomb()) {
        activateBombColor(object->colorIndex(), marked);
        return;
    }

    activateConnectedColor(object->row(), object->column(), object->colorIndex(), marked);
}

void Field::activateConnectedColor(int row, int column, int colorIndex, MarkedCells& marked) {
    if (!inside(row, column)) {
        return;
    }

    std::queue<CellPosition> queue;
    queue.push({row, column});

    while (!queue.empty()) {
        const CellPosition position = queue.front();
        queue.pop();

        const int currentRow = position.first;
        const int currentColumn = position.second;
        if (!inside(currentRow, currentColumn)) {
            continue;
        }

        IGameObject* object = cell(currentRow, currentColumn);
        if (object == nullptr || object->colorIndex() != colorIndex) {
            continue;
        }

        const int currentIndex = index(currentRow, currentColumn);
        if (marked[currentIndex]) {
            continue;
        }

        marked[currentIndex] = true;
        if (object->isBomb()) {
            activateBombColor(colorIndex, marked);
        }

        queue.push({currentRow - 1, currentColumn});
        queue.push({currentRow + 1, currentColumn});
        queue.push({currentRow, currentColumn - 1});
        queue.push({currentRow, currentColumn + 1});
    }
}

void Field::activateBombColor(int colorIndex, MarkedCells& marked) {
    for (int row = 0; row < Size; ++row) {
        for (int column = 0; column < Size; ++column) {
            IGameObject* object = cell(row, column);
            if (object != nullptr && object->colorIndex() == colorIndex) {
                marked[index(row, column)] = true;
            }
        }
    }
}

void Field::startDestruction(const MarkedCells& marked) {
    for (int i = 0; i < CellCount; ++i) {
        if (marked[i] && cells_[i] != nullptr) {
            cells_[i]->destroy();
        }
    }
}
