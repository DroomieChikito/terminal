#ifndef __HEADLESS_CONTAINER_H__
#define __HEADLESS_CONTAINER_H__

#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <QObject>
#include <QStringList>
#include "ApplicationSettings.h"
#include "DataConnectionListener.h"
#include "SignContainer.h"
#include "ZMQ_BIP15X_DataConnection.h"

#include "headless.pb.h"

namespace spdlog {
   class logger;
}
namespace bs {
   class SettlementAddressEntry;
   namespace hd {
      class Wallet;
   }
}

class ApplicationSettings;
class ConnectionManager;
class DataConnection;
class HeadlessListener;
class QProcess;
class WalletsManager;
class ZmqBIP15XDataConnection;

class HeadlessContainer : public SignContainer
{
   Q_OBJECT
public:
   HeadlessContainer(const std::shared_ptr<spdlog::logger> &, OpMode);
   ~HeadlessContainer() noexcept = default;

   bs::signer::RequestId signTXRequest(const bs::core::wallet::TXSignRequest &, bool autoSign = false
      , TXSignMode mode = TXSignMode::Full, const PasswordType& password = {}
      , bool keepDuplicatedRecipients = false) override;
   bs::signer::RequestId signPartialTXRequest(const bs::core::wallet::TXSignRequest &
      , bool autoSign = false, const PasswordType& password = {}) override;
   bs::signer::RequestId signPayoutTXRequest(const bs::core::wallet::TXSignRequest &, const bs::Address &authAddr
      , const std::string &settlementId, bool autoSign = false, const PasswordType& password = {}) override;

   bs::signer::RequestId signMultiTXRequest(const bs::core::wallet::TXMultiSignRequest &) override;

   void SendPassword(const std::string &walletId, const PasswordType &password,
      bool cancelledByUser) override;

   bs::signer::RequestId CancelSignTx(const BinaryData &txId) override;

   bs::signer::RequestId SetUserId(const BinaryData &) override;
   bs::signer::RequestId createHDLeaf(const std::string &rootWalletId, const bs::hd::Path &
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}) override;
   bs::signer::RequestId createHDWallet(const std::string &name, const std::string &desc
      , bool primary, const bs::core::wallet::Seed &seed
      , const std::vector<bs::wallet::PasswordData> &pwdData = {}, bs::wallet::KeyRank keyRank = { 0, 0 }) override;
   bs::signer::RequestId DeleteHDRoot(const std::string &rootWalletId) override;
   bs::signer::RequestId DeleteHDLeaf(const std::string &leafWalletId) override;
   bs::signer::RequestId getDecryptedRootKey(const std::string &walletId, const SecureBinaryData &password = {}) override;
   bs::signer::RequestId GetInfo(const std::string &rootWalletId) override;
   void setLimits(const std::string &walletId, const SecureBinaryData &password, bool autoSign) override;
   bs::signer::RequestId customDialogRequest(bs::signer::ui::DialogType signerDialog, const QVariantMap &data = QVariantMap()) override;

   void createSettlementWallet(const std::function<void(const std::shared_ptr<bs::sync::SettlementWallet> &)> &) override;

   void syncWalletInfo(const std::function<void(std::vector<bs::sync::WalletInfo>)> &) override;
   void syncHDWallet(const std::string &id, const std::function<void(bs::sync::HDWalletData)> &) override;
   void syncWallet(const std::string &id, const std::function<void(bs::sync::WalletData)> &) override;
   void syncAddressComment(const std::string &walletId, const bs::Address &, const std::string &) override;
   void syncTxComment(const std::string &walletId, const BinaryData &, const std::string &) override;
   void syncNewAddress(const std::string &walletId, const std::string &index, AddressEntryType
      , const std::function<void(const bs::Address &)> &) override;
   void syncNewAddresses(const std::string &walletId, const std::vector<std::pair<std::string, AddressEntryType>> &
      , const std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)> &
      , bool persistent = true) override;

   bool isReady() const override;
   bool isWalletOffline(const std::string &walletId) const override;

protected:
   bs::signer::RequestId Send(Blocksettle::Communication::headless::RequestPacket, bool incSeqNo = true);
   void ProcessSignTXResponse(unsigned int id, const std::string &data);
   void ProcessPasswordRequest(const std::string &data);
   void ProcessCreateHDWalletResponse(unsigned int id, const std::string &data);
   bs::signer::RequestId SendDeleteHDRequest(const std::string &rootWalletId, const std::string &leafId);
   void ProcessGetRootKeyResponse(unsigned int id, const std::string &data);
   void ProcessGetHDWalletInfoResponse(unsigned int id, const std::string &data);
   void ProcessSetLimitsResponse(unsigned int id, const std::string &data);
   void ProcessSyncWalletInfo(unsigned int id, const std::string &data);
   void ProcessSyncHDWallet(unsigned int id, const std::string &data);
   void ProcessSyncWallet(unsigned int id, const std::string &data);
   void ProcessSyncAddresses(unsigned int id, const std::string &data);
   void ProcessSettlWalletCreate(unsigned int id, const std::string &data);

   std::shared_ptr<HeadlessListener>   listener_;
   std::unordered_set<std::string>     missingWallets_;
   std::set<bs::signer::RequestId>     signRequests_;
   std::map<bs::signer::RequestId, std::function<void(const std::shared_ptr<bs::sync::SettlementWallet> &)>>   cbSettlWalletMap_;
   std::map<bs::signer::RequestId, std::function<void(std::vector<bs::sync::WalletInfo>)>>  cbWalletInfoMap_;
   std::map<bs::signer::RequestId, std::function<void(bs::sync::HDWalletData)>>  cbHDWalletMap_;
   std::map<bs::signer::RequestId, std::function<void(bs::sync::WalletData)>>    cbWalletMap_;
   std::map<bs::signer::RequestId, std::function<void(const std::vector<std::pair<bs::Address, std::string>> &)>> cbNewAddrsMap_;
};

bool KillHeadlessProcess();


class RemoteSigner : public HeadlessContainer
{
   Q_OBJECT
public:
   RemoteSigner(const std::shared_ptr<spdlog::logger> &, const QString &host
      , const QString &port, NetworkType netType
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , const std::shared_ptr<ApplicationSettings>& appSettings
      , OpMode opMode = OpMode::Remote);
   ~RemoteSigner() noexcept = default;

   bool Start() override;
   bool Stop() override;
   bool Connect() override;
   bool Disconnect() override;
   bool isOffline() const override;
   bool hasUI() const override;
   SecureBinaryData getOwnPubKey() const { return connection_->getOwnPubKey(); }

protected slots:
   void onAuthenticated();
   void onConnected();
   void onDisconnected();
   void onConnError(const QString &err);
   void onPacketReceived(Blocksettle::Communication::headless::RequestPacket);

private:
   void ConnectHelper();
   void Authenticate();

protected:
   const QString                              host_;
   const QString                              port_;
   const NetworkType                          netType_;
   std::shared_ptr<ZmqBIP15XDataConnection>   connection_;
   std::shared_ptr<ApplicationSettings>       appSettings_;

private:
   std::shared_ptr<ConnectionManager> connectionManager_;
   mutable std::mutex   mutex_;
};

class LocalSigner : public RemoteSigner
{
   Q_OBJECT
public:
   LocalSigner(const std::shared_ptr<spdlog::logger> &, const QString &homeDir
      , NetworkType, const QString &port
      , const std::shared_ptr<ConnectionManager>& connectionManager
      , const std::shared_ptr<ApplicationSettings>& appSettings
      , SignContainer::OpMode mode = OpMode::Local
      , double asSpendLimit = 0);
   ~LocalSigner() noexcept = default;

   bool Start() override;
   bool Stop() override;

protected:
   virtual QStringList args() const;
   virtual QString pidFileName() const;

private:
   const QString  homeDir_;
   const double   asSpendLimit_;
   std::shared_ptr<QProcess>  headlessProcess_;
};


class HeadlessListener : public QObject, public DataConnectionListener
{
   Q_OBJECT
public:
   HeadlessListener(const std::shared_ptr<spdlog::logger> &logger
      , const std::shared_ptr<DataConnection> &conn, NetworkType netType)
      : logger_(logger), connection_(conn), netType_(netType) {}

   void OnDataReceived(const std::string& data) override;
   void OnConnected() override;
   void OnDisconnected() override;
   void OnError(DataConnectionError errorCode) override;

   bs::signer::RequestId Send(Blocksettle::Communication::headless::RequestPacket
      , bool updateId = true);
   bs::signer::RequestId newRequestId() { return ++id_; }
   bool hasUI() const { return hasUI_; }

signals:
   void authenticated();
   void authFailed();
   void connected();
   void disconnected();
   void error(const QString &err);
   void PacketReceived(Blocksettle::Communication::headless::RequestPacket);

private:
   std::shared_ptr<spdlog::logger>  logger_;
   std::shared_ptr<DataConnection>  connection_;
   const NetworkType                netType_;
   bs::signer::RequestId            id_ = 0;
   bool     hasUI_ = false;
};

#endif // __HEADLESS_CONTAINER_H__
