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

double MatchObject::SmoothStep(double value) {
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

void MatchObject::Update(double dt) {
    blinkTime_ += dt;

    std::visit(MakeOverloaded(
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
            const double progress = SmoothStep(state.elapsed / state.duration);
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

void MatchObject::Render(QPainter& painter, const QRectF& boardRect, double cellSize) {
    if (destroyed_) {
        return;
    }

    const QPointF objectCenter = GetCenter(boardRect, cellSize);
    const double objectScale = std::visit(MakeOverloaded(
        [](const Appearing& state) -> double {
            return SmoothStep(state.elapsed / state.duration);
        },
        [](const Idle&) -> double {
            return 1.0;
        },
        [](const Falling&) -> double {
            return 1.0;
        },
        [](const Destroying& state) -> double {
            return 1.0 - SmoothStep(state.elapsed / state.duration);
        }
    ), state_);
    const double radius = cellSize * (0.5 - ObjectPadding) * objectScale;
    if (radius <= 0.0) {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(GetColor());
    painter.drawEllipse(objectCenter, radius, radius);
}

int MatchObject::GetRow() const {
    return row_;
}

int MatchObject::GetColumn() const {
    return column_;
}

int MatchObject::GetColorIndex() const {
    return colorIndex_;
}

bool MatchObject::IsBomb() const {
    return bomb_;
}

bool MatchObject::Contains(const QPointF& point, const QRectF& boardRect, double cellSize) const {
    if (destroyed_ || IsAnimating()) {
        return false;
    }

    const QPointF objectCenter = GetCenter(boardRect, cellSize);
    const double radius = cellSize * (0.5 - ObjectPadding);
    const double dx = point.x() - objectCenter.x();
    const double dy = point.y() - objectCenter.y();

    return dx * dx + dy * dy <= radius * radius;
}

bool MatchObject::IsAnimating() const {
    return !std::holds_alternative<Idle>(state_);
}

bool MatchObject::IsDestroyed() const {
    return destroyed_;
}

void MatchObject::MoveTo(int row, int column) {
    if (row_ == row && column_ == column) {
        return;
    }

    const double distance = std::abs(row - row_);
    state_ = Falling{visualRow_, static_cast<double>(row), 0.0, 0.13 + distance * 0.055};
    row_ = row;
    column_ = column;
}

void MatchObject::Destroy() {
    if (std::holds_alternative<Destroying>(state_) || destroyed_) {
        return;
    }

    state_ = Destroying{};
}

QColor MatchObject::GetColor() const {
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

QPointF MatchObject::GetCenter(const QRectF& boardRect, double cellSize) const {
    return QPointF(boardRect.left() + (column_ + 0.5) * cellSize,
                   boardRect.top() + (visualRow_ + 0.5) * cellSize);
}

Field::Field() {
    for (int row = 0; row < kSize; ++row) {
        for (int column = 0; column < kSize; ++column) {
            CreateObject(row, column);
        }
    }
}

void Field::Update(double dt) {
    for (IGameObject* object : cells_) {
        if (object != nullptr) {
            object->Update(dt);
        }
    }

    CollectDestroyed();

    if (HasAnimations()) {
        return;
    }

    if (CollapseAndFill()) {
        return;
    }

    ActivateMatches();
}

void Field::Render(QPainter& painter) {
    const QRectF bounds = painter.viewport();
    const double boardSize = std::min(bounds.width(), bounds.height()) * 0.92;
    cellSize_ = boardSize / kSize;
    boardRect_ = QRectF(bounds.center().x() - boardSize * 0.5,
                        bounds.center().y() - boardSize * 0.5,
                        boardSize,
                        boardSize);

    painter.save();
    painter.setPen(QPen(QColor(45, 45, 48), 1.0));
    painter.setBrush(QColor(15, 15, 18));
    painter.drawRect(boardRect_);

    for (int row = 1; row < kSize; ++row) {
        const double y = boardRect_.top() + row * cellSize_;
        painter.drawLine(QPointF(boardRect_.left(), y), QPointF(boardRect_.right(), y));
    }

    for (int column = 1; column < kSize; ++column) {
        const double x = boardRect_.left() + column * cellSize_;
        painter.drawLine(QPointF(x, boardRect_.top()), QPointF(x, boardRect_.bottom()));
    }

    for (IGameObject* object : cells_) {
        if (object != nullptr) {
            object->Render(painter, boardRect_, cellSize_);
        }
    }

    painter.restore();
}

void Field::Click(int x, int y) {
    if (cellSize_ <= 0.0 || HasAnimations()) {
        return;
    }

    const QPointF point(x, y);
    MarkedCells marked{};

    for (IGameObject* object : cells_) {
        if (object != nullptr && object->Contains(point, boardRect_, cellSize_)) {
            ActivateObject(object, &marked);
            StartDestruction(marked);
            return;
        }
    }
}

int Field::GetIndex(int row, int column) const {
    return row * kSize + column;
}

bool Field::IsInside(int row, int column) const {
    return row >= 0 && row < kSize && column >= 0 && column < kSize;
}

const IGameObject* Field::GetCell(int row, int column) const {
    return cells_[GetIndex(row, column)];
}

IGameObject* Field::GetCell(int row, int column) {
    return cells_[GetIndex(row, column)];
}

void Field::SetCell(int row, int column, IGameObject* object) {
    cells_[GetIndex(row, column)] = object;
}

void Field::CreateObject(int row, int column) {
    const int color = GetRandomColorFor(row, column);
    const bool bomb = QRandomGenerator::global()->generateDouble() < kBombChance;
    SetCell(row, column, objectPool_.Create(row, column, color, bomb));
}

int Field::GetRandomColorFor(int row, int column) const {
    std::array<int, kColorCount> colors{};
    for (int i = 0; i < kColorCount; ++i) {
        colors[i] = i;
    }

    for (int i = kColorCount - 1; i > 0; --i) {
        const int swapIndex = QRandomGenerator::global()->bounded(i + 1);
        std::swap(colors[i], colors[swapIndex]);
    }

    for (int color : colors) {
        if (!DoesColorCreateMatch(row, column, color)) {
            return color;
        }
    }

    return colors.front();
}

bool Field::DoesColorCreateMatch(int row, int column, int colorIndex) const {
    int horizontal = 1;
    for (int c = column - 1; c >= 0 && GetCell(row, c) != nullptr && GetCell(row, c)->GetColorIndex() == colorIndex; --c) {
        ++horizontal;
    }
    for (int c = column + 1; c < kSize && GetCell(row, c) != nullptr && GetCell(row, c)->GetColorIndex() == colorIndex; ++c) {
        ++horizontal;
    }

    int vertical = 1;
    for (int r = row - 1; r >= 0 && GetCell(r, column) != nullptr && GetCell(r, column)->GetColorIndex() == colorIndex; --r) {
        ++vertical;
    }
    for (int r = row + 1; r < kSize && GetCell(r, column) != nullptr && GetCell(r, column)->GetColorIndex() == colorIndex; ++r) {
        ++vertical;
    }

    return horizontal >= 3 || vertical >= 3;
}

bool Field::HasAnimations() const {
    return std::any_of(cells_.begin(), cells_.end(), [](const IGameObject* object) {
        return object != nullptr && object->IsAnimating();
    });
}

void Field::CollectDestroyed() {
    for (IGameObject*& object : cells_) {
        if (object != nullptr && object->IsDestroyed()) {
            objectPool_.Destroy(static_cast<MatchObject*>(object));
            object = nullptr;
        }
    }
}

bool Field::CollapseAndFill() {
    bool changed = false;

    for (int column = 0; column < kSize; ++column) {
        int writeRow = kSize - 1;

        for (int row = kSize - 1; row >= 0; --row) {
            IGameObject* object = GetCell(row, column);
            if (object == nullptr) {
                continue;
            }

            SetCell(row, column, nullptr);
            SetCell(writeRow, column, object);
            object->MoveTo(writeRow, column);
            changed = changed || row != writeRow;
            --writeRow;
        }

        for (int row = writeRow; row >= 0; --row) {
            CreateObject(row, column);
            changed = true;
        }
    }

    return changed;
}

bool Field::ActivateMatches() {
    MarkedCells matched{};

    for (int row = 0; row < kSize; ++row) {
        int start = 0;
        while (start < kSize) {
            IGameObject* object = GetCell(row, start);
            if (object == nullptr) {
                ++start;
                continue;
            }

            const int color = object->GetColorIndex();
            int end = start + 1;
            while (end < kSize && GetCell(row, end) != nullptr && GetCell(row, end)->GetColorIndex() == color) {
                ++end;
            }

            if (end - start >= 3) {
                for (int column = start; column < end; ++column) {
                    ActivateConnectedColor(row, column, color, &matched);
                }
            }

            start = end;
        }
    }

    for (int column = 0; column < kSize; ++column) {
        int start = 0;
        while (start < kSize) {
            IGameObject* object = GetCell(start, column);
            if (object == nullptr) {
                ++start;
                continue;
            }

            const int color = object->GetColorIndex();
            int end = start + 1;
            while (end < kSize && GetCell(end, column) != nullptr && GetCell(end, column)->GetColorIndex() == color) {
                ++end;
            }

            if (end - start >= 3) {
                for (int row = start; row < end; ++row) {
                    ActivateConnectedColor(row, column, color, &matched);
                }
            }

            start = end;
        }
    }

    const bool anyMatched = std::any_of(matched.begin(), matched.end(), [](bool value) {
        return value;
    });
    if (anyMatched) {
        StartDestruction(matched);
    }

    return anyMatched;
}

void Field::ActivateObject(IGameObject* object, MarkedCells* marked) {
    if (object->IsBomb()) {
        ActivateBombColor(object->GetColorIndex(), marked);
        return;
    }

    ActivateConnectedColor(object->GetRow(), object->GetColumn(), object->GetColorIndex(), marked);
}

void Field::ActivateConnectedColor(int row, int column, int colorIndex, MarkedCells* marked) {
    if (!IsInside(row, column)) {
        return;
    }

    std::queue<CellPosition> queue;
    queue.push({row, column});

    while (!queue.empty()) {
        const CellPosition position = queue.front();
        queue.pop();

        const int currentRow = position.first;
        const int currentColumn = position.second;
        if (!IsInside(currentRow, currentColumn)) {
            continue;
        }

        IGameObject* object = GetCell(currentRow, currentColumn);
        if (object == nullptr || object->GetColorIndex() != colorIndex) {
            continue;
        }

        const int currentIndex = GetIndex(currentRow, currentColumn);
        if ((*marked)[currentIndex]) {
            continue;
        }

        (*marked)[currentIndex] = true;
        if (object->IsBomb()) {
            ActivateBombColor(colorIndex, marked);
        }

        queue.push({currentRow - 1, currentColumn});
        queue.push({currentRow + 1, currentColumn});
        queue.push({currentRow, currentColumn - 1});
        queue.push({currentRow, currentColumn + 1});
    }
}

void Field::ActivateBombColor(int colorIndex, MarkedCells* marked) {
    for (int row = 0; row < kSize; ++row) {
        for (int column = 0; column < kSize; ++column) {
            IGameObject* object = GetCell(row, column);
            if (object != nullptr && object->GetColorIndex() == colorIndex) {
                (*marked)[GetIndex(row, column)] = true;
            }
        }
    }
}

void Field::StartDestruction(const MarkedCells& marked) {
    for (int i = 0; i < kCellCount; ++i) {
        if (marked[i] && cells_[i] != nullptr) {
            cells_[i]->Destroy();
        }
    }
}
