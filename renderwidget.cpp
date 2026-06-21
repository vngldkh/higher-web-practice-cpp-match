#include "renderwidget.h"

#include <QPainter>
#include <QMouseEvent>

#include "field.h"

RenderWidget::RenderWidget(QWidget *parent)
    : QWidget{parent}
{
    setAttribute(Qt::WA_OpaquePaintEvent);

    connect(&timer_, &QTimer::timeout, this, &RenderWidget::tick);

    timer_.start(16);
    frameTimer_.start();
}

RenderWidget::~RenderWidget()
{

}

void RenderWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::black);

    field_.Render(painter);
}

void RenderWidget::mousePressEvent(QMouseEvent *event)
{
    field_.Click(event->position().x(), event->position().y());
    QWidget::mousePressEvent(event);
}

void RenderWidget::tick()
{
    double dt = frameTimer_.restart() / 1000.0;

    field_.Update(dt);

    update();
}
