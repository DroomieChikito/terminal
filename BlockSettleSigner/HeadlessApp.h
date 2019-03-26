#ifndef __HEADLESS_APP_H__
#define __HEADLESS_APP_H__

#include <functional>
#include <memory>
#include <QObject>
#include "CoreWallet.h"
#include "EncryptionUtils.h"
#include "SignContainer.h"

namespace spdlog {
   class logger;
}
namespace bs {
   namespace core {
      class WalletsManager;
   }
}
class HeadlessContainerListener;
class QProcess;
class SignerAdapterListener;
class SignerSettings;
class ZmqSecuredServerConnection;


class HeadlessAppObj : public QObject
{
   Q_OBJECT

public:
   HeadlessAppObj(const std::shared_ptr<spdlog::logger> &
      , const std::shared_ptr<SignerSettings> &);

   void start();
   void setReadyCallback(const std::function<void(bool)> &cb) { cbReady_ = cb; }
   void setCallbacks(const std::function<void(const std::string &)> &cbPeerConn
      , const std::function<void(const std::string &)> &cbPeerDisconn
      , const std::function<void(const bs::core::wallet::TXSignRequest &, const std::string &)> &cbPwd
      , const std::function<void(const BinaryData &)> &cbTxSigned
      , const std::function<void(const BinaryData &)> &cbCancelTxSign
      , const std::function<void(int64_t, bool)> &cbXbtSpent
      , const std::function<void(const std::string &)> &cbAsAct
      , const std::function<void(const std::string &)> &cbAsDeact);

   void reloadWallets(const std::string &, const std::function<void()> &);
   void reconnect(const std::string &listenAddr, const std::string &port);
   void setOnline(bool);
   void setLimits(SignContainer::Limits);
   void passwordReceived(const std::string &walletId, const SecureBinaryData &, bool cancelledByUser);
   void close();

signals:
   void finished();

private:
   void startInterface();
   void onlineProcessing();

private:

   std::shared_ptr<spdlog::logger>  logger_;
   const std::shared_ptr<SignerSettings>        settings_;
   std::shared_ptr<bs::core::WalletsManager>    walletsMgr_;
   std::shared_ptr<ZmqSecuredServerConnection>  connection_;
   std::shared_ptr<HeadlessContainerListener>   listener_;
   SecureBinaryData                             zmqPubKey_;
   SecureBinaryData                             zmqPrvKey_;
   std::shared_ptr<SignerAdapterListener>       adapterLsn_;
   std::shared_ptr<QProcess>  guiProcess_;
   std::function<void(bool)>  cbReady_ = nullptr;
};

#endif // __HEADLESS_APP_H__
