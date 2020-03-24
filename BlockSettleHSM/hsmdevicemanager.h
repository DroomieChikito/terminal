/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef HSMDEVICESCANNER_H
#define HSMDEVICESCANNER_H

#include "trezor/trezorStructure.h"
#include <memory>

#include <QObject>
#include <QVector>
#include <QStringListModel>

class TrezorClient;
class ConnectionManager;

class HSMDeviceManager : public QObject
{
   Q_OBJECT
   Q_PROPERTY(QStringListModel* devices READ devices NOTIFY devicesChanged)
public:
   HSMDeviceManager(const std::shared_ptr<ConnectionManager>& connectionManager, bool testNet, QObject* parent = nullptr);
    ~HSMDeviceManager() override;

   // Property
   QStringListModel* devices();

   Q_INVOKABLE void scanDevices();
   Q_INVOKABLE void requestPublicKey(int deviceIndex);
   Q_INVOKABLE void setMatrixPin(int deviceIndex, QString pin);
   Q_INVOKABLE void cancel(int deviceIndex);

   Q_INVOKABLE void prepareTrezorForSign(QString deviceId);
   Q_INVOKABLE void signTX(int outputs_count, int inputs_count);

   Q_INVOKABLE void releaseDevices();

signals:
   void devicesChanged();
   void publicKeyReady(QString xpub, QString label, QString vendor);
   void requestPinMatrix();

   void deviceNotFound(QString deviceId);
   void deviceReady(QString deviceId);

public:
   std::unique_ptr<TrezorClient> trezorClient_;
   QVector<DeviceKey> devices_;
   QStringListModel* model_;
   bool testNet_{};
};

#endif // HSMDEVICESCANNER_H
