/*

***********************************************************************************
* Copyright (C) 2016 - 2019, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#include "ReqXBTSettlementContainer.h"

#include <QApplication>

#include <spdlog/spdlog.h>

#include "AssetManager.h"
#include "AuthAddressManager.h"
#include "CheckRecipSigner.h"
#include "CurrencyPair.h"
#include "QuoteProvider.h"
#include "SignContainer.h"
#include "TradesUtils.h"
#include "UiUtils.h"
#include "Wallets/SyncHDWallet.h"
#include "Wallets/SyncWalletsManager.h"

#include <sstream>

using namespace bs::sync;

Q_DECLARE_METATYPE(AddressVerificationState)

ReqXBTSettlementContainer::ReqXBTSettlementContainer(const std::shared_ptr<spdlog::logger> &logger
   , const std::shared_ptr<AuthAddressManager> &authAddrMgr
   , const std::shared_ptr<SignContainer> &signContainer
   , const std::shared_ptr<ArmoryConnection> &armory
   , const std::shared_ptr<bs::sync::hd::Wallet> &xbtWallet
   , const std::shared_ptr<bs::sync::WalletsManager> &walletsMgr
   , const bs::network::RFQ &rfq
   , const bs::network::Quote &quote
   , const bs::Address &authAddr
   , const std::map<UTXO, std::string> &utxosPayinFixed
   , const bs::Address &recvAddrIfSet)
   : bs::SettlementContainer()
   , logger_(logger)
   , authAddrMgr_(authAddrMgr)
   , walletsMgr_(walletsMgr)
   , signContainer_(signContainer)
   , armory_(armory)
   , xbtWallet_(xbtWallet)
   , rfq_(rfq)
   , quote_(quote)
   , recvAddrIfSet_(recvAddrIfSet)
   , weSellXbt_(!rfq.isXbtBuy())
   , authAddr_(authAddr)
   , utxosPayinFixed_(utxosPayinFixed)
{
   assert(authAddr.isValid());

   qRegisterMetaType<AddressVerificationState>();

   connect(signContainer_.get(), &SignContainer::TXSigned, this, &ReqXBTSettlementContainer::onTXSigned);

   connect(this, &ReqXBTSettlementContainer::timerExpired, this, &ReqXBTSettlementContainer::onTimerExpired);

   CurrencyPair cp(quote_.security);
   const bool isFxProd = (quote_.product != bs::network::XbtCurrency);
   fxProd_ = cp.ContraCurrency(bs::network::XbtCurrency);
   amount_ = isFxProd ? quantity() / price() : quantity();

   comment_ = std::string(bs::network::Side::toString(bs::network::Side::invert(quote_.side))) + " "
      + quote_.security + " @ " + std::to_string(price());
}

ReqXBTSettlementContainer::~ReqXBTSettlementContainer() = default;

void ReqXBTSettlementContainer::acceptSpotXBT()
{
   emit acceptQuote(rfq_.requestId, "not used");
}

bool ReqXBTSettlementContainer::cancel()
{
   deactivate();
   emit settlementCancelled();

   SettlementContainer::releaseUtxoRes();

   return true;
}

void ReqXBTSettlementContainer::onTimerExpired()
{
   cancel();
}

void ReqXBTSettlementContainer::activate()
{
   startTimer(kWaitTimeoutInSec);

   settlementIdHex_ = quote_.settlementId;

   addrVerificator_ = std::make_shared<AddressVerificator>(logger_, armory_
      , [this, handle = validityFlag_.handle()](const bs::Address &address, AddressVerificationState state)
   {
      QMetaObject::invokeMethod(qApp, [this, handle, address, state] {
         if (!handle.isValid()) {
            return;
         }
         dealerAuthAddress_ = address;
         dealerVerifStateChanged(state);
      });
   });

   addrVerificator_->SetBSAddressList(authAddrMgr_->GetBSAddresses());

   settlementId_ = BinaryData::CreateFromHex(quote_.settlementId);
   userKey_ = BinaryData::CreateFromHex(quote_.requestorAuthPublicKey);
   dealerAuthKey_ = BinaryData::CreateFromHex(quote_.dealerAuthPublicKey);

   acceptSpotXBT();
}

void ReqXBTSettlementContainer::deactivate()
{
   stopTimer();
}

bs::sync::PasswordDialogData ReqXBTSettlementContainer::toPasswordDialogData(QDateTime timestamp) const
{
   bs::sync::PasswordDialogData dialogData = SettlementContainer::toPasswordDialogData(timestamp);
   dialogData.setValue(PasswordDialogData::Market, "XBT");
   dialogData.setValue(PasswordDialogData::AutoSignCategory, static_cast<int>(bs::signer::AutoSignCategory::SettlementRequestor));

   // rfq details
   QString qtyProd = UiUtils::XbtCurrency;
   QString fxProd = QString::fromStdString(fxProd_);

   dialogData.setValue(PasswordDialogData::Title, tr("Settlement Pay-In"));
   dialogData.setValue(PasswordDialogData::Price, UiUtils::displayPriceXBT(price()));
   dialogData.setValue(PasswordDialogData::FxProduct, fxProd);

   bool isFxProd = (quote_.product != bs::network::XbtCurrency);

   if (isFxProd) {
      dialogData.setValue(PasswordDialogData::Quantity, tr("%1 %2")
                    .arg(UiUtils::displayAmountForProduct(quantity(), fxProd, bs::network::Asset::Type::SpotXBT))
                    .arg(fxProd));

      dialogData.setValue(PasswordDialogData::TotalValue, tr("%1 XBT")
                    .arg(UiUtils::displayAmount(quantity() / price())));
   }
   else {
      dialogData.setValue(PasswordDialogData::Quantity, tr("%1 XBT")
                    .arg(UiUtils::displayAmount(amount())));

      dialogData.setValue(PasswordDialogData::TotalValue, tr("%1 %2")
                    .arg(UiUtils::displayAmountForProduct(amount() * price(), fxProd, bs::network::Asset::Type::SpotXBT))
                    .arg(fxProd));
   }

   // settlement details
   dialogData.setValue(PasswordDialogData::SettlementId, settlementId_.toHexStr());
   dialogData.setValue(PasswordDialogData::SettlementAddress, settlAddr_.display());

   dialogData.setValue(PasswordDialogData::RequesterAuthAddress, authAddr_.display());
   dialogData.setValue(PasswordDialogData::RequesterAuthAddressVerified, true);

   dialogData.setValue(PasswordDialogData::ResponderAuthAddress, 
      bs::Address::fromPubKey(dealerAuthKey_, AddressEntryType_P2WPKH).display());
   dialogData.setValue(PasswordDialogData::ResponderAuthAddressVerified, dealerVerifState_ == AddressVerificationState::Verified);


   // tx details
   dialogData.setValue(PasswordDialogData::TxInputProduct, UiUtils::XbtCurrency);

   return dialogData;
}

void ReqXBTSettlementContainer::dealerVerifStateChanged(AddressVerificationState state)
{
   dealerVerifState_ = state;
   bs::sync::PasswordDialogData pd;
   pd.setValue(PasswordDialogData::SettlementId, settlementId_.toHexStr());
   pd.setValue(PasswordDialogData::ResponderAuthAddress, dealerAuthAddress_.display());
   pd.setValue(PasswordDialogData::ResponderAuthAddressVerified, state == AddressVerificationState::Verified);
   pd.setValue(PasswordDialogData::SigningAllowed, state == AddressVerificationState::Verified);
   signContainer_->updateDialogData(pd);
}

void ReqXBTSettlementContainer::cancelWithError(const QString& errorMessage)
{
   emit error(errorMessage);
   cancel();

   // Call failed to remove from RfqStorage and cleanup memory
   emit failed();
}

void ReqXBTSettlementContainer::onTXSigned(unsigned int id, BinaryData signedTX
   , bs::error::ErrorCode errCode, std::string errTxt)
{
   if ((payoutSignId_ != 0) && (payoutSignId_ == id)) {
      payoutSignId_ = 0;

      if ((errCode != bs::error::ErrorCode::NoError) || signedTX.isNull()) {
         logger_->warn("[ReqXBTSettlementContainer::onTXSigned] Pay-Out sign failure: {} ({})"
            , (int)errCode, errTxt);
         cancelWithError(tr("Pay-Out signing failed: %1").arg(bs::error::ErrorCodeToString(errCode)));
         return;
      }

      SPDLOG_LOGGER_DEBUG(logger_, "signed payout: {}", signedTX.toHexStr());

      bs::tradeutils::PayoutVerifyArgs verifyArgs;
      verifyArgs.signedTx = signedTX;
      verifyArgs.settlAddr = settlAddr_;
      verifyArgs.usedPayinHash = usedPayinHash_;
      verifyArgs.amount = bs::XBTAmount(amount_);
      auto verifyResult = bs::tradeutils::verifySignedPayout(verifyArgs);
      if (!verifyResult.success) {
         SPDLOG_LOGGER_ERROR(logger_, "payout verification failed: {}", verifyResult.errorMsg);
         cancelWithError(tr("payin verification failed: %1").arg(bs::error::ErrorCodeToString(errCode)));
         return;
      }

      emit sendSignedPayoutToPB(settlementIdHex_, signedTX);

      for (const auto &leaf : xbtWallet_->getGroup(xbtWallet_->getXBTGroupType())->getLeaves()) {
         leaf->setTransactionComment(signedTX, comment_);
      }
//         walletsMgr_->getSettlementWallet()->setTransactionComment(payoutData_, comment_); //TODO: later

      // OK. if payout created - settletlement accepted for this RFQ
      deactivate();
      emit settlementAccepted();

      // Call completed to remove from RfqStorage and cleanup memory
      emit completed();
   }

   if ((payinSignId_ != 0) && (payinSignId_ == id)) {
      payinSignId_ = 0;

      if ((errCode != bs::error::ErrorCode::NoError) || signedTX.isNull()) {
         SPDLOG_LOGGER_ERROR(logger_, "failed to create pay-in TX: {} ({})", static_cast<int>(errCode), errTxt);
         cancelWithError(tr("Failed to create Pay-In TX: %1").arg(bs::error::ErrorCodeToString(errCode)));
         return;
      }

      emit sendSignedPayinToPB(settlementIdHex_, signedTX);

      for (const auto &leaf : xbtWallet_->getGroup(xbtWallet_->getXBTGroupType())->getLeaves()) {
         leaf->setTransactionComment(signedTX, comment_);
      }
//    walletsMgr_->getSettlementWallet()->setTransactionComment(signedTX, comment_);  //TODO: later

      // OK. if payin created - settletlement accepted for this RFQ
      deactivate();
      emit settlementAccepted();

      // Call completed to remove from RfqStorage and cleanup memory
      emit completed();
   }
}

void ReqXBTSettlementContainer::onUnsignedPayinRequested(const std::string& settlementId)
{
   if (settlementIdHex_ != settlementId) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid id : {} . {} expected", settlementId, settlementIdHex_);
      return;
   }

   if (!weSellXbt_) {
      SPDLOG_LOGGER_ERROR(logger_, "customer buy on thq rfq {}. should not create unsigned payin"
         , settlementId);
      return;
   }

   SPDLOG_LOGGER_DEBUG(logger_, "unsigned payin requested: {}", settlementId);

   bs::tradeutils::PayinArgs args;
   initTradesArgs(args, settlementId);
   args.fixedInputs.reserve(utxosPayinFixed_.size());
   for (const auto &input : utxosPayinFixed_) {
      args.fixedInputs.push_back(input.first);
   }
   for (const auto &leaf : xbtWallet_->getGroup(xbtWallet_->getXBTGroupType())->getLeaves()) {
      args.inputXbtWallets.push_back(leaf);
   }
   args.utxoReservation = bs::UtxoReservation::instance();
   args.utxoReservationWalletId = xbtWallet_->walletId();

   auto payinCb = bs::tradeutils::PayinResultCb([this, handle = validityFlag_.handle()]
      (bs::tradeutils::PayinResult result)
   {
      QMetaObject::invokeMethod(qApp, [this, handle, result = std::move(result)] {
         if (!handle.isValid()) {
            return;
         }

         if (!result.success) {
            SPDLOG_LOGGER_ERROR(logger_, "payin sign request creation failed: {}", result.errorMsg);
            cancelWithError(tr("payin failed"));
            return;
         }

         settlAddr_ = result.settlementAddr;

         const auto list = authAddrMgr_->GetVerifiedAddressList();
         const auto userAddress = bs::Address::fromPubKey(userKey_, AddressEntryType_P2WPKH);
         userKeyOk_ = (std::find(list.begin(), list.end(), userAddress) != list.end());
         if (!userKeyOk_) {
            SPDLOG_LOGGER_WARN(logger_, "userAddr {} not found in verified addrs list ({})"
               , userAddress.display(), list.size());
            return;
         }

         const auto dealerAddrSW = bs::Address::fromPubKey(dealerAuthKey_, AddressEntryType_P2WPKH);
         addrVerificator_->addAddress(dealerAddrSW);
         addrVerificator_->startAddressVerification();

         unsignedPayinRequest_ = std::move(result.signRequest);

         utxoRes_ = bs::UtxoReservationToken::makeNewReservation(logger_, unsignedPayinRequest_, id());

         emit sendUnsignedPayinToPB(settlementIdHex_, bs::network::UnsignedPayinData{ unsignedPayinRequest_.serializeState(), std::move(result.preimageData)} );
      });
   });

   bs::tradeutils::createPayin(std::move(args), std::move(payinCb));
}

void ReqXBTSettlementContainer::onSignedPayoutRequested(const std::string& settlementId, const BinaryData& payinHash, QDateTime timestamp)
{
   if (settlementIdHex_ != settlementId) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid id : {} . {} expected", settlementId, settlementIdHex_);
      return;
   }

   startTimer(kWaitTimeoutInSec);

   SPDLOG_LOGGER_DEBUG(logger_, "create payout for {} on {} for {}", settlementId, payinHash.toHexStr(), amount_);
   usedPayinHash_ = payinHash;

   bs::tradeutils::PayoutArgs args;
   initTradesArgs(args, settlementId);
   args.payinTxId = payinHash;
   args.recvAddr = recvAddrIfSet_;
   args.outputXbtWallet = xbtWallet_->getGroup(xbtWallet_->getXBTGroupType())->getLeaves().at(0);

   auto payoutCb = bs::tradeutils::PayoutResultCb([this, payinHash, timestamp, handle = validityFlag_.handle()]
      (bs::tradeutils::PayoutResult result)
   {
      QMetaObject::invokeMethod(qApp, [this, payinHash, handle, timestamp, result = std::move(result)] {
         if (!handle.isValid()) {
            return;
         }

         if (!result.success) {
            SPDLOG_LOGGER_ERROR(logger_, "creating payout failed: {}", result.errorMsg);
            cancelWithError(tr("payout failed"));
            return;
         }

         settlAddr_ = result.settlementAddr;

         bs::sync::PasswordDialogData dlgData = toPayOutTxDetailsPasswordDialogData(result.signRequest, timestamp);
         dlgData.setValue(PasswordDialogData::Market, "XBT");
         dlgData.setValue(PasswordDialogData::SettlementId, settlementId_.toHexStr());
         dlgData.setValue(PasswordDialogData::ResponderAuthAddressVerified, true);
         dlgData.setValue(PasswordDialogData::SigningAllowed, true);

         SPDLOG_LOGGER_DEBUG(logger_, "pay-out fee={}, qty={} ({}), payin hash={}"
            , result.signRequest.fee, amount_, amount_ * BTCNumericTypes::BalanceDivider, payinHash.toHexStr(true));

         payoutSignId_ = signContainer_->signSettlementPayoutTXRequest(result.signRequest
            , {settlementId_, dealerAuthKey_, true}, dlgData);
      });
   });
   bs::tradeutils::createPayout(std::move(args), std::move(payoutCb));
}

void ReqXBTSettlementContainer::onSignedPayinRequested(const std::string& settlementId, const BinaryData& unsignedPayin, QDateTime timestamp)
{
   if (settlementIdHex_ != settlementId) {
      SPDLOG_LOGGER_ERROR(logger_, "invalid id : {} . {} expected", settlementId, settlementIdHex_);
      return;
   }

   startTimer(kWaitTimeoutInSec);

   if (!weSellXbt_) {
      SPDLOG_LOGGER_ERROR(logger_, "customer buy on thq rfq {}. should not sign payin", settlementId);
      return;
   }

   if (!unsignedPayinRequest_.isValid()) {
      SPDLOG_LOGGER_ERROR(logger_, "unsigned payin request is invalid: {}", settlementIdHex_);
      return;
   }

   SPDLOG_LOGGER_DEBUG(logger_, "signed payin requested {}", settlementId);

   // XXX check unsigned payin?

   bs::sync::PasswordDialogData dlgData = toPasswordDialogData(timestamp);
   dlgData.setValue(PasswordDialogData::SettlementPayInVisible, true);

   payinSignId_ = signContainer_->signSettlementTXRequest(unsignedPayinRequest_, dlgData);
}

void ReqXBTSettlementContainer::initTradesArgs(bs::tradeutils::Args &args, const std::string &settlementId)
{
   args.amount = bs::XBTAmount{amount_};
   args.settlementId = BinaryData::CreateFromHex(settlementId);
   args.ourAuthAddress = authAddr_;
   args.cpAuthPubKey = dealerAuthKey_;
   args.walletsMgr = walletsMgr_;
   args.armory = armory_;
   args.signContainer = signContainer_;
}