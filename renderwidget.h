#ifndef RENDERWIDGET_H
#define RENDERWIDGET_H

#include <QWidget>
#include <QElapsedTimer>
#include <QTimer>

#include "field.h"

class RenderWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RenderWidget(QWidget *parent = nullptr);
    ~RenderWidget();

protected:
    void paintEvent(QPaintEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void tick();

private:
    Field field_;
    QTimer timer_;
    QElapsedTimer frameTimer_;
};

#endif // RENDERWIDGET_H
