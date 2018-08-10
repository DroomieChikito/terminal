#include "TransactionsViewModel.h"

#include "CheckRecipSigner.h"
#include "LedgerEntryData.h"
#include "PyBlockDataManager.h"
#include "SafeLedgerDelegate.h"
#include "UiUtils.h"
#include "WalletsManager.h"

#include <QDateTime>
#include <QMutexLocker>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

using namespace std;


TransactionsViewModel::TransactionsViewModel(std::shared_ptr<PyBlockDataManager> bdm, const std::shared_ptr<WalletsManager>& walletsManager
   , const std::shared_ptr<SafeLedgerDelegate> &ledgerDelegate, QObject* parent, const std::shared_ptr<bs::Wallet> &defWlt)
   : QAbstractTableModel(parent)
   , bdm_(bdm)
   , ledgerDelegate_(ledgerDelegate)
   , walletsManager_(walletsManager)
   , threadPool_(this)
   , defaultWallet_(defWlt)
   , stopped_(false)
   , refreshing_(false)
   , colorGray_(Qt::darkGray), colorRed_(Qt::red), colorYellow_(Qt::darkYellow), colorGreen_(Qt::darkGreen), colorInvalid_(Qt::red)
   , initialLoadCompleted_(false)
{
   fontBold_.setBold(true);
   qRegisterMetaType<TransactionsViewItem>();
   qRegisterMetaType<TransactionItems>();
   threadPool_.setMaxThreadCount(2);

   auto watcher = new  QFutureWatcher<void>(this);
   connect(watcher, SIGNAL(finished()), this, SLOT(onRawDataLoaded()));
   const auto futLoadData = QtConcurrent::run(&threadPool_, this, &TransactionsViewModel::loadLedgerEntries);
   watcher->setFuture(futLoadData);

   if (bdm_) {
      connect(bdm_.get(), &PyBlockDataManager::zeroConfReceived, this, &TransactionsViewModel::onZeroConf, Qt::QueuedConnection);
      connect(bdm_.get(), &PyBlockDataManager::goingOffline, this, &TransactionsViewModel::onArmoryOffline, Qt::QueuedConnection);
   }
   connect(walletsManager_.get(), &WalletsManager::walletChanged, this, &TransactionsViewModel::refresh, Qt::QueuedConnection);
   connect(walletsManager_.get(), &WalletsManager::blockchainEvent, this, &TransactionsViewModel::updatePage, Qt::QueuedConnection);
   connect(walletsManager_.get(), &WalletsManager::walletsReady, this, &TransactionsViewModel::updatePage, Qt::QueuedConnection);
   connect(this, &TransactionsViewModel::itemsAdded, this, &TransactionsViewModel::onNewItems, Qt::QueuedConnection);
   connect(this, &TransactionsViewModel::rowUpdated, this, &TransactionsViewModel::onRowUpdated, Qt::QueuedConnection);
   connect(this, &TransactionsViewModel::itemsDeleted, this, &TransactionsViewModel::onItemsDeleted, Qt::QueuedConnection);
   connect(this, &TransactionsViewModel::itemConfirmed, this, &TransactionsViewModel::onItemConfirmed, Qt::QueuedConnection);
}

TransactionsViewModel::~TransactionsViewModel()
{
   stopped_ = true;
   threadPool_.clear();
   threadPool_.waitForDone();
}

int TransactionsViewModel::columnCount(const QModelIndex &) const
{
   return static_cast<int>(Columns::last);
}

int TransactionsViewModel::rowCount(const QModelIndex &parent) const
{
   if (parent.isValid()) {
      return 0;
   }
   return (int)currentPage_.size();
}

QVariant TransactionsViewModel::data(const QModelIndex &index, int role) const
{
   const auto col = static_cast<Columns>(index.column());

   if (role == Qt::TextAlignmentRole) {
      switch (col) {
      case Columns::Amount:   return Qt::AlignRight;
      case Columns::RbfFlag:  return Qt::AlignCenter;
      default:  break;
      }
      return {};
   }

   const unsigned int row = index.row();
   if (row >= currentPage_.size()) {
      return {};
   }
   const auto &item = currentPage_[row];

   if (role == Qt::DisplayRole) {
      switch(col) {
      case Columns::Date:
         return item.displayDateTime;
      case Columns::Status:
         return tr("   %1").arg(item.confirmations);
      case Columns::Wallet:
         return item.walletName;
      case Columns::SendReceive:
         return item.dirStr;
      case Columns::Comment:
         return item.comment;
      case Columns::Amount:
         return item.amountStr;
      case Columns::Address:
         return UiUtils::displayAddress(item.mainAddress);
      case Columns::RbfFlag:
         if (!item.confirmations) {
            if (item.led->isOptInRBF()) {
               return tr("RBF");
            }
            else if (item.isCPFP) {
               return tr("CPFP");
            }
         }
         break;
/*      case Columns::MissedBlocks:
         return item.confirmations < 6 ? 0 : QVariant();*/
      default:
         return QVariant();
      }
   }
   else if (role == WalletRole) {
      return qVariantFromValue(static_cast<void*>(item.wallet.get()));
   }
   else if (role == SortRole) {
      switch(col) {
      case Columns::Date:        return item.led->getTxTime();
      case Columns::Status:      return item.confirmations;
      case Columns::Wallet:      return item.walletName;
      case Columns::SendReceive: return (int)item.direction;
      case Columns::Comment:     return item.comment;
      case Columns::Amount:      return QVariant::fromValue<double>(qAbs(item.amount));
      case Columns::Address:     return item.mainAddress;
      default:    return QVariant();
      }
   }
   else if (role == Qt::TextColorRole) {
      switch (col) {
         case Columns::Address:
         case Columns::Wallet:
            return colorGray_;

         case Columns::Status:
         {
            if (item.confirmations == 0) {
               return colorRed_;
            }
            else if (item.confirmations < 6) {
               return colorYellow_;
            }
            else {
               return colorGreen_;
            }
         }

         default:
            if (!item.isValid) {
               return colorInvalid_;
            }
            else {
               return QVariant();
            }
      }
   }
   else if (role == Qt::FontRole) {
      bool boldFont = false;
      if (col == Columns::Amount) {
         boldFont = true;
      }
      else if ((col == Columns::Status) && (item.confirmations < 6)) {
         boldFont = true;
      }
      if (boldFont) {
         return fontBold_;
      }
   }
   else if (role == FilterRole) {
      switch (col)
      {
         case Columns::Date:        return item.led->getTxTime();
         case Columns::Wallet:      return item.walletID;
         case Columns::SendReceive: return item.direction;
         default:    return QVariant();
      }
   }

   return QVariant();
}

QVariant TransactionsViewModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   if ((role == Qt::DisplayRole) && (orientation == Qt::Horizontal)) {
      switch(static_cast<Columns>(section)) {
      case Columns::Date:           return tr("Date");
      case Columns::Status:         return tr("Confirmations");
      case Columns::Wallet:         return tr("Wallet");
      case Columns::SendReceive:    return tr("Type");
      case Columns::Comment:        return tr("Comment");
      case Columns::Address:        return tr("Address");
      case Columns::Amount:         return tr("Amount");
      case Columns::RbfFlag:        return tr("Flag");
//      case Columns::MissedBlocks:   return tr("Missed Blocks");
      default:    return QVariant();
      }
   }
   return QVariant();
}

void TransactionsViewModel::refresh()
{
   if (refreshing_) {
      return;
   }
   clear();
   updatePage();
}

void TransactionsViewModel::updatePage()
{
   QtConcurrent::run(&threadPool_, this, &TransactionsViewModel::loadNewTransactions);
}

void TransactionsViewModel::clear()
{
   beginResetModel();
   {
      QMutexLocker locker(&updateMutex_);
      currentPage_.clear();
      currentKeys_.clear();
   }
   endResetModel();
}

void TransactionsViewModel::onZeroConf(std::vector<LedgerEntryData> page)
{
   QtConcurrent::run(&threadPool_, this, &TransactionsViewModel::insertNewTransactions, page);
}

TransactionsViewItem TransactionsViewModel::itemFromTransaction(const LedgerEntryData& led)
{
   TransactionsViewItem item;
   item.led = std::make_shared<LedgerEntryData>(led);
   item.displayDateTime = UiUtils::displayDateTime(led.getTxTime());
   item.walletID = QString::fromStdString(led.getWalletID());
   item.wallet = walletsManager_->GetWalletById(led.getWalletID());
   if (!item.wallet && defaultWallet_) {
      item.wallet = defaultWallet_;
   }

   if (item.led->getBlockNum() < uint32_t(-1)) {
      item.confirmations = walletsManager_->GetTopBlockHeight() + 1 - item.led->getBlockNum();
   }
   if (item.wallet) {
      item.walletName = QString::fromStdString(item.wallet->GetWalletName());
   }
   item.isValid = item.wallet ? item.wallet->isTxValid(item.led->getTxHash()) : false;
   return item;
}

static std::string mkTxKey(const LedgerEntryData &item)
{
   return item.getTxHash().toBinStr() + item.getWalletID();
}
static std::string mkTxKey(const TransactionsViewItem &item)
{
   return mkTxKey(*(item.led.get()));
}

bool TransactionsViewModel::txKeyExists(const std::string &key)
{
   return (currentKeys_.find(key) != currentKeys_.end());
}

void TransactionsViewModel::loadNewTransactions()
{
   bool expected = false;
   if (!std::atomic_compare_exchange_strong(&refreshing_, &expected, true)) {
      return;
   }

   std::vector<LedgerEntryData> allPages = walletsManager_->getTxPage(0);

   insertNewTransactions(allPages);
   updateBlockHeight(allPages);
   refreshing_.store(false);
}

void TransactionsViewModel::insertNewTransactions(const std::vector<LedgerEntryData> &page)
{
   if (!initialLoadCompleted_) {
      return;
   }
   TransactionItems newItems;
   newItems.reserve(page.size());
   const auto &settlWallet = walletsManager_->GetSettlementWallet();

   for (const auto led : page) {
      if (settlWallet && settlWallet->isTempWalletId(led.getWalletID())) {
         continue;
      }
      const auto item = itemFromTransaction(led);
      if (!item.isValid) {
         continue;
      }
      const auto txKey = mkTxKey(item);
      {
         QMutexLocker locker(&updateMutex_);
         if (txKeyExists(txKey)) {
            continue;
         }
         currentKeys_.insert(txKey);
      }
      newItems.push_back(item);
   }

   if (!newItems.empty()) {
      emit itemsAdded(newItems);
   }
}

void TransactionsViewModel::updateBlockHeight(const std::vector<LedgerEntryData> &page)
{
   std::unordered_map<std::string, LedgerEntryData> newData;
   for (const auto &item : page) {
      newData[mkTxKey(item)] = item;
   }
   int firstRow = -1, lastRow = 0;
   std::vector<TransactionsViewItem> firstConfItems;
   {
      QMutexLocker locker(&updateMutex_);
      for (size_t i = 0; i < currentPage_.size(); i++) {
         auto &item = currentPage_[i];
         const auto itPage = newData.find(mkTxKey(item));
         if (itPage != newData.end()) {
            if (item.led->getValue() != itPage->second.getValue()) {
               item.led = std::make_shared<LedgerEntryData>(itPage->second);
               item.amountStr.clear();
               item.calcAmount(bdm_, walletsManager_);
            }
            item.isValid = item.wallet->isTxValid(item.led->getTxHash());

            if (itPage->second.getBlockNum() < uint32_t(-1)) {
               item.confirmations = walletsManager_->GetTopBlockHeight() + 1 - itPage->second.getBlockNum();
               if (item.confirmations == 1) {
                  firstConfItems.push_back(item);
               }
            }
            if (firstRow < 0) {
               firstRow = i;
            }
            if (lastRow < (int)i) {
               lastRow = i;
            }
         }
      }
   }
   if (firstRow >= 0) {
      emit dataChanged(index(firstRow, static_cast<int>(Columns::Status))
         , index(lastRow, static_cast<int>(Columns::Status)));
   }
   for (const auto &item : firstConfItems) {
      emit itemConfirmed(item);
   }
}

void TransactionsViewModel::loadLedgerEntries()
{
   size_t pageId = 0;
   if (ledgerDelegate_ == nullptr) {
      return;
   }

   while (true) {
      auto nextPage = ledgerDelegate_->getHistoryPage(pageId++);
      if (nextPage.empty()) {
         break;
      }
      {
         QMutexLocker locker(&updateMutex_);
         rawData_.insert(rawData_.end(), nextPage.begin(), nextPage.end());
      }
   }
}

void TransactionsViewModel::onRawDataLoaded()
{
   auto watcher = new  QFutureWatcher<void>(this);
   connect(watcher, SIGNAL(finished()), this, SLOT(onDataLoaded()));
   const auto futLoadData = QtConcurrent::run(&threadPool_, this, &TransactionsViewModel::ledgerToTxData);
   watcher->setFuture(futLoadData);
}

void TransactionsViewModel::ledgerToTxData()
{
   QMutexLocker locker(&updateMutex_);
   currentPage_.reserve(rawData_.size());
   for (const auto &led : rawData_) {
      const auto item = itemFromTransaction(led);
      if (!item.isValid) {
         continue;
      }
      const auto txKey = mkTxKey(item);
      if (txKeyExists(txKey)) {
         continue;
      }
      currentKeys_.insert(txKey);
      currentPage_.emplace_back(item);
   }
   initialLoadCompleted_ = true;
}

void TransactionsViewModel::onDataLoaded()
{
   emit layoutChanged();
   QtConcurrent::run(&threadPool_, this, &TransactionsViewModel::loadTransactionDetails, 0, rawData_.size());
   emit dataLoaded(currentPage_.size());
}

void TransactionsViewModel::onNewItems(const TransactionItems items)
{
   QMutexLocker locker(&updateMutex_);
   unsigned int curLastIdx = currentPage_.size();
   beginInsertRows(QModelIndex(), curLastIdx, curLastIdx + items.size() - 1);
   currentPage_.insert(currentPage_.end(), items.begin(), items.end());
   endInsertRows();

   QtConcurrent::run(&threadPool_, this, &TransactionsViewModel::loadTransactionDetails, curLastIdx, items.size());
}

int TransactionsViewModel::getItemIndex(const TransactionsViewItem &item) const
{
   QMutexLocker locker(&updateMutex_);
   for (int i = 0; i < currentPage_.size(); i++) {
      const auto &curItem = currentPage_[i];
      if ((item.led->getTxHash() == curItem.led->getTxHash()) && (item.walletID == curItem.walletID)) {
         return i;
      }
   }
   return -1;
}

void TransactionsViewModel::onItemsDeleted(const TransactionItems items)
{
   for (const auto &item : items) {
      const int idx = getItemIndex(item);
      if (idx < 0) {
         continue;
      }
      beginRemoveRows(QModelIndex(), idx, idx);
      {
         QMutexLocker locker(&updateMutex_);
         currentPage_.erase(currentPage_.begin() + idx);
      }
      endRemoveRows();
   }
}

void TransactionsViewModel::onRowUpdated(int row, TransactionsViewItem item, int c1, int c2)
{
   if ((row < 0) && (updRowFirst_ >= 0)) {   // Final flush
      emit dataChanged(index(updRowFirst_, c1), index(updRowLast_, c2));
      updRowFirst_ = -1;
      return;
   }
   if ((row < 0) || !item.led) {
      return;
   }

   int idx = -1;
   {
      const auto itemKey = mkTxKey(item);
      QMutexLocker locker(&updateMutex_);
      for (int i = qMax<int>(0, row - 5); i < qMin<int>(currentPage_.size(), row + 5); i++) {
         if (mkTxKey(currentPage_[i]) == itemKey) {
            currentPage_[i] = item;
            idx = i;
            break;
         }
      }
   }
   if (idx >= 0) {
      if (updRowFirst_ < 0) {
         updRowFirst_ = updRowLast_ = idx;
      }
      else {   // Update list in chunks to prevent signals queue overflow (repro'd on Windows)
         if (((idx - updRowLast_) > 1) || (idx < updRowFirst_) || ((updRowLast_ - updRowFirst_) > 9)) {
            emit dataChanged(index(updRowFirst_, c1), index(updRowLast_, c2));
            updRowFirst_ = updRowLast_ = idx;
         }
         else {
            updRowLast_ = idx;
         }
      }
   }
}

void TransactionsViewModel::onItemConfirmed(const TransactionsViewItem item)
{
   if (!item.tx.isInitialized()) {
      return;
   }

   TransactionItems doubleSpendItems;
   {
      QMutexLocker locker(&updateMutex_);

      for (size_t i = 0; i < currentPage_.size(); i++) {
         const auto &curItem = currentPage_[i];
         if (curItem.confirmations) {
            continue;
         }
         if (curItem.containsInputsFrom(item.tx)) {
            doubleSpendItems.push_back(curItem);
         }
      }
   }
   if (!doubleSpendItems.empty()) {
      emit itemsDeleted(doubleSpendItems);
   }
}

bool TransactionsViewModel::isTransactionVerified(int transactionRow) const
{
   QMutexLocker locker(&updateMutex_);
   const auto& item = currentPage_[transactionRow];
   return walletsManager_->IsTransactionVerified(*item.led.get());
}

TransactionsViewItem TransactionsViewModel::getItem(int row) const
{
   QMutexLocker locker(&updateMutex_);
   if ((row < 0) || (row >= currentPage_.size())) {
      return {};
   }
   return currentPage_[row];
}

std::shared_ptr<WalletsManager> TransactionsViewModel::GetWalletsManager() const
{
   return walletsManager_;
}

std::shared_ptr<PyBlockDataManager> TransactionsViewModel::GetBlockDataManager() const
{
   return bdm_;
}

void TransactionsViewModel::loadTransactionDetails(unsigned int iStart, size_t count)
{
   const auto pageSize = currentPage_.size();

   std::map<int, TransactionsViewItem> uninitedItems;
   {
      QMutexLocker locker(&updateMutex_);
      for (unsigned int i = iStart; i < qMin<unsigned int>(pageSize, iStart + count); i++) {
         if (pageSize > currentPage_.size()) {
            break;
         }
         const TransactionsViewItem &item = currentPage_[i];
         if (!item.initialized) {
            uninitedItems[i] = item;
         }
      }
   }
   for (auto item : uninitedItems) {
      if (stopped_) {
         break;
      }
      updateTransactionDetails(item.second, item.first);
   }
   emit rowUpdated(-1, {}, static_cast<int>(Columns::SendReceive), static_cast<int>(Columns::Address));
}

void TransactionsViewModel::updateTransactionDetails(TransactionsViewItem &item, int index)
{
   item.initialize(bdm_, walletsManager_);
   emit rowUpdated(index, item, static_cast<int>(Columns::SendReceive), static_cast<int>(Columns::Amount));
}

void TransactionsViewItem::initialize(const std::shared_ptr<PyBlockDataManager> &bdm, const std::shared_ptr<WalletsManager> &walletsManager)
{
   if (initialized) {
      return;
   }
   if (!tx.isInitialized()) {
      tx = bdm->getTxByHash(led->getTxHash());
   }
   direction = walletsManager->GetTransactionDirection(tx, wallet);
   dirStr = QObject::tr(bs::Transaction::toStringDir(direction));
   if (amountStr.isEmpty()) {
      calcAmount(bdm, walletsManager);
   }
   mainAddress = walletsManager->GetTransactionMainAddress(tx, wallet, (amount > 0));
   comment = wallet ? QString::fromStdString(wallet->GetTransactionComment(led->getTxHash())) : QString();

   auto endLineIndex = comment.indexOf(QLatin1Char('\n'));
   if (endLineIndex != -1) {
      comment = comment.left(endLineIndex) + QLatin1String("...");
   }

   initialized = true;
}


static bool isSpecialWallet(const std::shared_ptr<bs::Wallet> &wallet)
{
   if (!wallet) {
      return false;
   }
   switch (wallet->GetType()) {
   case bs::wallet::Type::Settlement:
   case bs::wallet::Type::ColorCoin:
      return true;
   default: break;
   }
   return false;
}

void TransactionsViewItem::calcAmount(const std::shared_ptr<PyBlockDataManager> &bdm, const std::shared_ptr<WalletsManager> &walletsManager)
{
   if (wallet && bdm) {
      if (!tx.isInitialized()) {
         tx = bdm->getTxByHash(led->getTxHash());
      }
      if (!tx.isInitialized()) {
         return;
      }
      bool hasSpecialAddr = false;
      int64_t outputVal = 0;
      for (size_t i = 0; i < tx.getNumTxOut(); ++i) {
         TxOut out = tx.getTxOutCopy(i);
         if (led->isChainedZC() && !hasSpecialAddr) {
            const auto addr = bs::Address::fromTxOut(out);
            hasSpecialAddr = isSpecialWallet(walletsManager->GetWalletByAddress(addr.id()));
         }
         outputVal += out.getValue();
      }

      int64_t inputVal = 0;
      for (size_t i = 0; i < tx.getNumTxIn(); i++) {
         TxIn in = tx.getTxInCopy(i);
         OutPoint op = in.getOutPoint();
         Tx prevTx = bdm->getTxByHash(op.getTxHash());
         if (prevTx.isInitialized()) {
            TxOut prevOut = prevTx.getTxOutCopy(op.getTxOutIndex());
            inputVal += prevOut.getValue();
            if (led->isChainedZC() && !hasSpecialAddr) {
               const auto addr = bs::Address::fromTxOut(prevTx.getTxOutCopy(op.getTxOutIndex()));
               hasSpecialAddr = isSpecialWallet(walletsManager->GetWalletByAddress(addr.id()));
            }
         }
      }
      auto value = led->getValue();
      const auto fee = (wallet->GetType() == bs::wallet::Type::ColorCoin) || (value > 0) ? 0 : (outputVal - inputVal);
      value -= fee;
      amount = wallet->GetTxBalance(value);
      amountStr = wallet->displayTxValue(value);

      if (led->isChainedZC() && (wallet->GetType() == bs::wallet::Type::Bitcoin) && !hasSpecialAddr) {
         isCPFP = true;
      }
   }
   else {
      amount = led->getValue() / BTCNumericTypes::BalanceDivider;
      amountStr = UiUtils::displayAmount(amount);
   }
}

bool TransactionsViewItem::containsInputsFrom(const Tx &inTx) const
{
   const bs::TxChecker checker(tx);

   for (size_t i = 0; i < inTx.getNumTxIn(); i++) {
      TxIn in = inTx.getTxInCopy((int)i);
      if (!in.isInitialized()) {
         continue;
      }
      OutPoint op = in.getOutPoint();
      if (checker.hasInput(op.getTxHash())) {
         return true;
      }
   }
   return false;
}
