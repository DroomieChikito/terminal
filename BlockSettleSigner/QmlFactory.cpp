#include "QmlFactory.h"
#include "AuthProxy.h"
#include "WalletsManager.h"

using namespace bs::hd;
using namespace bs::wallet;


// todo
// check authObject->signWallet results, return null object, emit error signal

WalletInfo *QmlFactory::createWalletInfo() {
   auto wi = new bs::hd::WalletInfo();
   QQmlEngine::setObjectOwnership(wi, QQmlEngine::JavaScriptOwnership);
   return wi;
}

WalletInfo *QmlFactory::createWalletInfo(const QString &walletId) {
   // ? move logic to WalletsManager ?
   bs::hd::WalletInfo *wi = nullptr;

   const auto &wallet = walletsMgr_->GetWalletById(walletId.toStdString());
   if (wallet) {
      const std::shared_ptr<bs::hd::Wallet> &rootWallet = walletsMgr_->GetHDRootForLeaf(wallet->GetWalletId());
      wi = new bs::hd::WalletInfo(wallet, rootWallet);
      connect(rootWallet.get(), &bs::hd::Wallet::metaDataChanged, wi, [rootWallet, wi](){
         if (rootWallet) {
            wi->initEncKeys(rootWallet);
         }
      });
   }
   else {
      const auto &hdWallet = walletsMgr_->GetHDWalletById(walletId.toStdString());
      if (!hdWallet) {
         // wallet not found
         wi = new bs::hd::WalletInfo();
      }
      else {
         wi = new bs::hd::WalletInfo(hdWallet);
      }
   }

   QQmlEngine::setObjectOwnership(wi, QQmlEngine::JavaScriptOwnership);
   return wi;
}

WalletInfo *QmlFactory::createWalletInfoFromDigitalBackup(const QString &filename) {
   auto wi = new bs::hd::WalletInfo(bs::hd::WalletInfo::fromDigitalBackup(filename));
   QQmlEngine::setObjectOwnership(wi, QQmlEngine::JavaScriptOwnership);
   return wi;
}

AuthSignWalletObject *QmlFactory::createAutheIDSignObject(AutheIDClient::RequestType requestType
                                                          , WalletInfo *walletInfo)
{
   logger_->debug("[QmlFactory] signing {}", walletInfo->walletId().toStdString());
   AuthSignWalletObject *authObject = new AuthSignWalletObject(logger_, this);
   authObject->connectToServer();
   authObject->signWallet(requestType, walletInfo);
   QQmlEngine::setObjectOwnership(authObject, QQmlEngine::JavaScriptOwnership);
   return authObject;
}

AuthSignWalletObject *QmlFactory::createActivateEidObject(const QString &userId
                                                          , WalletInfo *walletInfo)
{
   logger_->debug("[QmlFactory] activate wallet {} for {}", walletInfo->walletId().toStdString(), userId.toStdString());
   AuthSignWalletObject *authObject = new AuthSignWalletObject(logger_, this);
   walletInfo->setEncKeys(QStringList() << (userId + QStringLiteral("::")));
   authObject->connectToServer();
   authObject->signWallet(AutheIDClient::ActivateWallet, walletInfo);
   QQmlEngine::setObjectOwnership(authObject, QQmlEngine::JavaScriptOwnership);
   return authObject;
}

AuthSignWalletObject *QmlFactory::createRemoveEidObject(int index
                                                        , WalletInfo *walletInfo)
{
   logger_->debug("[QmlFactory] remove device for {}, device index: {}", walletInfo->walletId().toStdString(), index);
   AuthSignWalletObject *authObject = new AuthSignWalletObject(logger_, this);
   authObject->connectToServer();
   authObject->removeDevice(index, walletInfo);
   QQmlEngine::setObjectOwnership(authObject, QQmlEngine::JavaScriptOwnership);
   return authObject;
}
