#include "ReqXBTSettlementContainer.h"
#include <spdlog/spdlog.h>
#include "AssetManager.h"
#include "AuthAddressManager.h"
#include "CheckRecipSigner.h"
#include "CurrencyPair.h"
#include "HDWallet.h"
#include "QuoteProvider.h"
#include "SignContainer.h"
#include "QuoteProvider.h"
#include "TransactionData.h"
#include "UiUtils.h"
#include "WalletsManager.h"

static const unsigned int kWaitTimeoutInSec = 30;

Q_DECLARE_METATYPE(AddressVerificationState)

ReqXBTSettlementContainer::ReqXBTSettlementContainer(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<AuthAddressManager> &authAddrMgr, const std::shared_ptr<AssetManager> &assetMgr
   , const std::shared_ptr<SignContainer> &signContainer, const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<WalletsManager> &walletsMgr, const bs::network::RFQ &rfq
   , const bs::network::Quote &quote, const std::shared_ptr<TransactionData> &txData)
   : bs::SettlementContainer(armory)
   , logger_(logger)
   , authAddrMgr_(authAddrMgr)
   , assetMgr_(assetMgr)
   , walletsMgr_(walletsMgr)
   , signContainer_(signContainer)
   , armory_(armory)
   , transactionData_(txData)
   , rfq_(rfq)
   , quote_(quote)
   , clientSells_(!rfq.isXbtBuy())
{
   qRegisterMetaType<AddressVerificationState>();

   utxoAdapter_ = std::make_shared<bs::UtxoReservation::Adapter>();
   bs::UtxoReservation::addAdapter(utxoAdapter_);

   connect(signContainer_.get(), &SignContainer::HDWalletInfo, this, &ReqXBTSettlementContainer::onHDWalletInfo);
   connect(signContainer_.get(), &SignContainer::TXSigned, this, &ReqXBTSettlementContainer::onTXSigned);

   connect(this, &ReqXBTSettlementContainer::timerExpired, this, &ReqXBTSettlementContainer::onTimerExpired);

   CurrencyPair cp(quote_.security);
   const bool isFxProd = (quote_.product != bs::network::XbtCurrency);
   fxProd_ = cp.ContraCurrency(bs::network::XbtCurrency);
   amount_ = isFxProd ? quantity() / price() : quantity();

   comment_ = std::string(bs::network::Side::toString(bs::network::Side::invert(quote_.side))) + " "
      + quote_.security + " @ " + std::to_string(price());

   dealerTx_ = BinaryData::CreateFromHex(quote_.dealerTransaction);
}

ReqXBTSettlementContainer::~ReqXBTSettlementContainer()
{
   bs::UtxoReservation::delAdapter(utxoAdapter_);
}

unsigned int ReqXBTSettlementContainer::createPayoutTx(const BinaryData& payinHash, double qty, const bs::Address &recvAddr
   , const SecureBinaryData &password)
{
   try {
      const auto &wallet = walletsMgr_->GetSettlementWallet();
      const auto txReq = wallet->CreatePayoutTXRequest(wallet->GetInputFromTX(settlAddr_, payinHash, qty), recvAddr
         , transactionData_->GetTransactionSummary().feePerByte);
      const auto authAddr = bs::Address::fromPubKey(userKey_, AddressEntryType_P2WPKH);
      logger_->debug("[ReqXBTSettlementContainer] pay-out fee={}, payin hash={}", txReq.fee, payinHash.toHexStr(true));

      return signContainer_->SignPayoutTXRequest(txReq, authAddr, settlAddr_, false, password);
   }
   catch (const std::exception &e) {
      logger_->warn("[ReqXBTSettlementContainer] failed to create pay-out transaction based on {}: {}"
         , payinHash.toHexStr(), e.what());
      emit error(tr("Pay-out transaction creation failure: %1").arg(QLatin1String(e.what())));
   }
   return 0;
}

void ReqXBTSettlementContainer::acceptSpotXBT(const SecureBinaryData &password)
{
   emit info(tr("Waiting for transactions signing..."));
   if (clientSells_) {
      const auto hasChange = transactionData_->GetTransactionSummary().hasChange;
      const auto changeAddr = hasChange ? transactionData_->GetWallet()->GetNewChangeAddress() : bs::Address();
      const auto payinTxReq = transactionData_->CreateTXRequest(false, changeAddr);
      payinSignId_ = signContainer_->SignTXRequest(payinTxReq, false, SignContainer::TXSignMode::Full
         , password);
      payoutPassword_ = password;
   }
   else {
      try {    // create payout based on dealer TX
         if (dealerTx_.isNull()) {
            logger_->error("[XBTSettlementTransactionWidget::acceptSpotXBT] empty dealer payin hash");
            emit error(tr("empty dealer payin hash"));
         }
         else {
            payoutSignId_ = createPayoutTx(dealerTx_, amount_, recvAddr_, password);
         }
      }
      catch (const std::exception &e) {
         logger_->warn("[XBTSettlementTransactionWidget::acceptSpotXBT] Pay-Out failed: {}", e.what());
         emit error(tr("Pay-Out to dealer failed: %1").arg(QString::fromStdString(e.what())));
         return;
      }
   }
}

bool ReqXBTSettlementContainer::accept(const SecureBinaryData &password)
{
   if (!payinReceived()) {
      acceptSpotXBT(password);
   }
   else {
      payoutSignId_ = createPayoutTx(Tx(payinData_).getThisHash(), amount_, recvAddr_, password);
   }
   return true;
}

bool ReqXBTSettlementContainer::cancel()
{
   deactivate();
   if (clientSells_) {
      if (!payoutData_.isNull()) {
         payoutOnCancel();
      }
      utxoAdapter_->unreserve(id());
   }
   emit settlementCancelled();
   return true;
}

void ReqXBTSettlementContainer::onTimerExpired()
{
   if (!clientSells_ && (waitForPayin_ || waitForPayout_)) {
      emit error(tr("Dealer didn't push transactions to blockchain within a given time interval"));
   }
   else {
      if (clientSells_ && waitForPayout_) {
         waitForPayout_ = false;
      }
   }
   cancel();
}

bool ReqXBTSettlementContainer::isAcceptable() const
{
   return (userKeyOk_ && (dealerVerifState_ == AddressVerificationState::Verified));
}

void ReqXBTSettlementContainer::activate()
{
   startTimer(kWaitTimeoutInSec);

   const auto &authWallet = walletsMgr_->GetAuthWallet();
   auto rootAuthWallet = walletsMgr_->GetHDRootForLeaf(authWallet->GetWalletId());
   authWalletName_ = rootAuthWallet->getName();
   authWalletId_ = rootAuthWallet->getWalletId();
   walletId_ = walletsMgr_->GetHDRootForLeaf(transactionData_->GetWallet()->GetWalletId())->getWalletId();

   if (clientSells_) {
      sellFromPrimary_ = (authWalletId_ == walletId_);

      emit info(tr("Enter password for \"%1\" wallet to sign Pay-In")
         .arg(QString::fromStdString(walletsMgr_->GetHDRootForLeaf(
            transactionData_->GetWallet()->GetWalletId())->getName())));

      if (!sellFromPrimary_) {
         infoReqIdAuth_ = signContainer_->GetInfo(rootAuthWallet->getWalletId());
      }
   }

   infoReqId_ = signContainer_->GetInfo(walletId_);

   addrVerificator_ = std::make_shared<AddressVerificator>(logger_, armory_, quote_.settlementId
      , [this](const std::shared_ptr<AuthAddress>& address, AddressVerificationState state)
   {
      dealerVerifStateChanged(state);
   });
   addrVerificator_->SetBSAddressList(authAddrMgr_->GetBSAddresses());

   settlementId_ = BinaryData::CreateFromHex(quote_.settlementId);
   userKey_ = BinaryData::CreateFromHex(quote_.requestorAuthPublicKey);
   const auto dealerAuthKey = BinaryData::CreateFromHex(quote_.dealerAuthPublicKey);
   const auto buyAuthKey = clientSells_ ? dealerAuthKey : userKey_;
   const auto sellAuthKey = clientSells_ ? userKey_ : dealerAuthKey;

   settlAddr_ = walletsMgr_->GetSettlementWallet()->newAddress(settlementId_, buyAuthKey, sellAuthKey, comment_);

   if (settlAddr_ == nullptr) {
      logger_->error("[ReqXBTSettlementContainer] failed to get settlement address");
      return;
   }

   recvAddr_ = transactionData_->GetFallbackRecvAddress();
   signContainer_->SyncAddresses({ {transactionData_->GetWallet(), recvAddr_} });

   const auto recipient = transactionData_->RegisterNewRecipient();
   transactionData_->UpdateRecipientAmount(recipient, amount_, transactionData_->maxSpendAmount());
   transactionData_->UpdateRecipientAddress(recipient, settlAddr_);

   const auto dealerAddrSW = bs::Address::fromPubKey(dealerAuthKey, AddressEntryType_P2WPKH);
   addrVerificator_->StartAddressVerification(std::make_shared<AuthAddress>(dealerAddrSW));
   addrVerificator_->RegisterBSAuthAddresses();
   addrVerificator_->RegisterAddresses();

   const auto list = authAddrMgr_->GetVerifiedAddressList();
   const auto userAddress = bs::Address::fromPubKey(userKey_, AddressEntryType_P2WPKH);
   userKeyOk_ = (std::find(list.begin(), list.end(), userAddress) != list.end());
   if (!userKeyOk_) {
      logger_->warn("[ReqXBTSettlementContainer::activate] userAddr {} not found in verified addrs list[{}]"
         , userAddress.display<std::string>(), list.size());
      return;
   }

   if (clientSells_) {
      if (!transactionData_->IsTransactionValid()) {
         userKeyOk_ = false;
         logger_->error("[ReqXBTSettlementContainer::activate] transaction data is invalid");
         emit error(tr("Transaction data is invalid - sending of pay-in is prohibited"));
         return;
      }
   }

   fee_ = transactionData_->GetTransactionSummary().totalFee;

   auto createMonitorCB = [this](const std::shared_ptr<bs::SettlementMonitorCb>& monitor)
   {
      monitor_ = monitor;

      monitor_->start([this](int, const BinaryData &) { onPayInZCDetected(); }
      , [this](int confNum, bs::PayoutSigner::Type signedBy) { onPayoutZCDetected(confNum, signedBy); }
      , [this](bs::PayoutSigner::Type) {});
   };

   walletsMgr_->GetSettlementWallet()->createMonitorCb(settlAddr_, logger_, createMonitorCB);
}

void ReqXBTSettlementContainer::deactivate()
{
   stopTimer();
   if (monitor_) {
      monitor_->stop();
   }
}

void ReqXBTSettlementContainer::zcReceived(const std::vector<bs::TXEntry>)
{
   if (monitor_) {
      monitor_->checkNewEntries();
   }
}

void ReqXBTSettlementContainer::dealerVerifStateChanged(AddressVerificationState state)
{
   dealerVerifState_ = state;
   emit DealerVerificationStateChanged(state);
}

void ReqXBTSettlementContainer::onTXSigned(unsigned int id, BinaryData signedTX,
   std::string errTxt, bool cancelledByUser)
{
   if (payinSignId_ && (payinSignId_ == id)) {
      payinSignId_ = 0;
      if (!errTxt.empty()) {
         emit error(tr("Failed to create Pay-In TX - re-type password and try again"));
         logger_->error("[ReqXBTSettlementContainer::onTXSigned] Failed to create pay-in TX: {}", errTxt);
         emit retry();
         return;
      }
      stopTimer();
      emit stop();
      payinData_ = signedTX;
      payoutSignId_ = createPayoutTx(Tx(payinData_).getThisHash(), amount_, recvAddr_, payoutPassword_);
      payoutPassword_.clear();
   }
   else if (payoutSignId_ && (payoutSignId_ == id)) {
      payoutSignId_ = 0;
      if (!errTxt.empty()) {
         logger_->warn("[ReqXBTSettlementContainer::onTXSigned] Pay-Out sign failure: {}", errTxt);
         emit error(tr("Pay-Out signing failed: %1").arg(QString::fromStdString(errTxt)));
         emit retry();
         return;
      }
      payoutData_ = signedTX;
      if (!clientSells_) {
         transactionData_->GetWallet()->SetTransactionComment(payoutData_, comment_);
         walletsMgr_->GetSettlementWallet()->SetTransactionComment(payoutData_, comment_);
      }

      emit info(tr("Waiting for Order verification"));

      waitForPayout_ = waitForPayin_ = true;
      emit acceptQuote(rfq_.requestId, payoutData_.toHexStr());
/*      quoteProvider_->AcceptQuote(QString::fromStdString(rfq_.requestId), quote_
         , payoutData_.toHexStr());*/
      startTimer(kWaitTimeoutInSec);
   }
}

void ReqXBTSettlementContainer::payoutOnCancel()
{
   utxoAdapter_->unreserve(id());

   if (!armory_->broadcastZC(payoutData_)) {
      logger_->error("[ReqXBTSettlementContainer::payoutOnCancel] failed to broadcast payout");
      return;
   }
   const std::string revokeComment = "Revoke for " + comment_;
   transactionData_->GetWallet()->SetTransactionComment(payoutData_, revokeComment);
   walletsMgr_->GetSettlementWallet()->SetTransactionComment(payoutData_, revokeComment);
}

void ReqXBTSettlementContainer::detectDealerTxs()
{
   if (clientSells_)  return;
   if (!waitForPayin_ && !waitForPayout_) {
      emit info(tr("Both pay-in and pay-out transactions were detected!"));
      logger_->debug("[ReqXBTSettlementContainer::detectDealerTxs] Both pay-in and pay-out transactions were detected on requester side");
      deactivate();
      emit settlementAccepted();
   }
}

void ReqXBTSettlementContainer::onPayInZCDetected()
{
   if (waitForPayin_ && !settlementId_.isNull()) {
      waitForPayin_ = false;

      if (clientSells_) {
         waitForPayout_ = true;
         startTimer(kWaitTimeoutInSec);
         emit info(tr("Waiting for dealer's pay-out in blockchain..."));
      }
      else {
         detectDealerTxs();
      }
   }
}

void ReqXBTSettlementContainer::onPayoutZCDetected(int confNum, bs::PayoutSigner::Type signedBy)
{
   Q_UNUSED(confNum);
   logger_->debug("[ReqXBTSettlementContainer::onPayoutZCDetected] signedBy={}"
      , bs::PayoutSigner::toString(signedBy));
   if (waitForPayout_) {
      waitForPayout_ = false;
   }

   if (!settlementId_.isNull()) {
      if (clientSells_) {
         deactivate();
         emit settlementAccepted();
      }
      else {
         detectDealerTxs();
      }
   }
}

void ReqXBTSettlementContainer::OrderReceived()
{
   if (clientSells_) {
      try {
         if (!armory_->broadcastZC(payinData_)) {
            throw std::runtime_error("Failed to bradcast transaction");
         }
         transactionData_->GetWallet()->SetTransactionComment(payinData_, comment_);
         walletsMgr_->GetSettlementWallet()->SetTransactionComment(payinData_, comment_);

         waitForPayin_ = true;
         logger_->debug("[XBTSettlementTransactionWidget::OrderReceived] Pay-In broadcasted");
         emit info(tr("Waiting for own pay-in in blockchain..."));
      }
      catch (const std::exception &e) {
         logger_->error("[XBTSettlementTransactionWidget::OrderReceived] Pay-In failed: {}", e.what());
         emit error(tr("Sending of Pay-In failed: %1").arg(QString::fromStdString(e.what())));
      }
   }
   else {
      emit info(tr("Waiting for dealer to broadcast both TXes to blockchain"));
   }
}

void ReqXBTSettlementContainer::onHDWalletInfo(unsigned int id, std::vector<bs::wallet::EncryptionType> encTypes
   , std::vector<SecureBinaryData> encKeys, bs::wallet::KeyRank keyRank)
{
   if (infoReqId_ && (id == infoReqId_)) {
      infoReqId_ = 0;
      encTypes_ = encTypes;
      encKeys_ = encKeys;
      keyRank_ = keyRank;
   }
   if (infoReqIdAuth_ && (id == infoReqIdAuth_)) {
      infoReqIdAuth_ = 0;
      encTypesAuth_ = encTypes;
      encKeysAuth_ = encKeys;
      keyRankAuth_ = keyRank;
      emit authWalletInfoReceived();
   }
}
