/****************************************************************************
 *
 * (c) 2009-2026 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include <QAbstractItemModel>
#include <QGeoCoordinate>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QVariant>

class GeoZoneOverlay : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(QAbstractItemModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    explicit GeoZoneOverlay(QQuickItem* parent = nullptr);
    void paint(QPainter* painter) override;

    QObject* map() const { return _map.data(); }
    QAbstractItemModel* model() const { return _model.data(); }

public slots:
    void setMap(QObject* map);
    void setModel(QAbstractItemModel* model);
    Q_INVOKABLE void refresh();

signals:
    void mapChanged();
    void modelChanged();

private slots:
    void onModelChanged();
    void onRowsInserted(const QModelIndex& parent, int first, int last);
    void onRowsRemoved(const QModelIndex& parent, int first, int last);
    void onModelReset();

private:
    void connectModel(QAbstractItemModel* model);
    QPointF mapPointForCoordinate(const QGeoCoordinate& coordinate) const;

    QPointer<QObject> _map;
    QPointer<QAbstractItemModel> _model;
};
