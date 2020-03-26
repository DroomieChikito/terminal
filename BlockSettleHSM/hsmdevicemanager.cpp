/*

***********************************************************************************
* Copyright (C) 2016 - , BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "hsmdevicemanager.h"
#include "trezor/trezorClient.h"
#include "trezor/trezorDevice.h"
#include "ConnectionManager.h"
#include "WalletManager.h"

HSMDeviceManager::HSMDeviceManager(const std::shared_ptr<ConnectionManager>& connectionManager, std::shared_ptr<bs::sync::WalletsManager> walletManager,
   bool testNet, QObject* parent /*= nullptr*/)
   : QObject(parent)
   , testNet_(testNet)
{
   trezorClient_ = std::make_unique<TrezorClient>(connectionManager, walletManager, testNet, this);
   model_ = new QStringListModel(this);
}

HSMDeviceManager::~HSMDeviceManager() = default;

void HSMDeviceManager::scanDevices()
{
   trezorClient_->initConnection([this]() {
      devices_.clear();
      devices_.append(trezorClient_->deviceKeys());
      emit devicesChanged();
   });
}

void HSMDeviceManager::requestPublicKey(int deviceIndex)
{
   auto device = trezorClient_->getTrezorDevice(devices_[deviceIndex].deviceId_);
   if (!device) {
      return;
   }

   device->getPublicKey([this, device](QByteArray&& xpub) {
      if (!device) {
         return;
      }
      const auto key = device->deviceKey();
      emit publicKeyReady(QString::fromStdString(xpub.toStdString()), key.deviceLabel_, key.deviceId_);
      releaseDevices();
   });

   connect(device, &TrezorDevice::requestPinMatrix,
      this, &HSMDeviceManager::requestPinMatrix, Qt::UniqueConnection);
}

void HSMDeviceManager::setMatrixPin(int deviceIndex, QString pin)
{
   auto device = trezorClient_->getTrezorDevice(devices_[deviceIndex].deviceId_);
   if (!device) {
      return;
   }

   device->setMatrixPin(pin.toStdString());
}

void HSMDeviceManager::cancel(int deviceIndex)
{
   auto device = trezorClient_->getTrezorDevice(devices_[deviceIndex].deviceId_);
   if (!device) {
      return;
   }

   device->cancel();
}

void HSMDeviceManager::prepareTrezorForSign(QString deviceId)
{
   trezorClient_->initConnection([this, deviceId]() {
      devices_.clear();

      for (auto key : trezorClient_->deviceKeys()) {
         if (key.deviceId_ == deviceId) {
            devices_.append(key);
         }
      }

      if (devices_.empty()) {
         emit deviceNotFound(deviceId);
      }
      else {
         emit deviceReady(deviceId);
      }
   });
}

void HSMDeviceManager::signTX(QVariant reqTX)
{
   auto device = trezorClient_->getTrezorDevice(devices_[0].deviceId_);
   if (!device) {
      return;
   }

   device->signTX(reqTX, [this](QByteArray&& resp) {
      BinaryData data  = BinaryData::fromString(resp.toStdString());
      auto hexw = data.toHexStr(true);
      txSigned({ data });
      releaseDevices();
   });

   connect(device, &TrezorDevice::requestPinMatrix,
      this, &HSMDeviceManager::requestPinMatrix, Qt::UniqueConnection);
   connect(device, &TrezorDevice::deviceTxStatusChanged,
      this, &HSMDeviceManager::deviceTxStatusChanged, Qt::UniqueConnection);
}

void HSMDeviceManager::releaseDevices()
{
   for (auto key : devices_) {
      auto device = trezorClient_->getTrezorDevice(key.deviceId_);
      if (device) {
         device->cancel();
      }
   }

   trezorClient_->releaseConnection();
}

QStringListModel* HSMDeviceManager::devices()
{
   QStringList labels;
   for (const auto& device : devices_)
      labels << device.deviceLabel_;

   model_->setStringList(labels);

   return model_;
}
