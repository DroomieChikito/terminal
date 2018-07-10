#include <chrono>
#include "QuoteRequestsModel.h"
#include "AssetManager.h"
#include "CelerSubmitQuoteNotifSequence.h"
#include "CurrencyPair.h"
#include "QuoteRequestsWidget.h"
#include "SettlementContainer.h"
#include "UiUtils.h"
#include "Colors.h"


QuoteRequestsModel::QuoteRequestsModel(const std::shared_ptr<bs::SecurityStatsCollector> &statsCollector
 , QObject* parent)
   : QAbstractItemModel(parent)
   , secStatsCollector_(statsCollector)
{
   timer_.setInterval(500);
   connect(&timer_, &QTimer::timeout, this, &QuoteRequestsModel::ticker);
   timer_.start();
}

QuoteRequestsModel::~QuoteRequestsModel()
{
   secStatsCollector_->saveState();
}

int QuoteRequestsModel::columnCount(const QModelIndex &) const
{
   return 10;
}

QVariant QuoteRequestsModel::data(const QModelIndex &index, int role) const
{
   IndexHelper *idx = static_cast<IndexHelper*>(index.internalPointer());

   switch (idx->type_) {
      case DataType::RFQ : {
         RFQ *r = static_cast<RFQ*>(idx->data_);

         switch (role) {
            case Qt::DisplayRole : {
               switch(index.column()) {
                  case Column::SecurityID : {
                     return r->security_;
                  }

                  case Column::Product : {
                     return r->product_;
                  }
                  case Column::Side : {
                     return r->side_;
                  }

                  case Column::Quantity : {
                     return r->quantityString_;
                  }

                  case Column::Party : {
                     return r->party_;
                  }

                  case Column::Status : {
                     return r->status_.status_;
                  }
                     break;

                  case Column::QuotedPx : {
                     return r->quotedPriceString_;
                  }

                  case Column::IndicPx : {
                     return r->indicativePxString_;
                  }

                  case Column::BestPx : {
                     return r->bestQuotedPxString_;
                  }

                  default:
                     return QVariant();
               }
            }
               break;

            case Role::ShowProgress : {
               return r->status_.showProgress_;
            }
               break;

            case Role::Timeout : {
               return r->status_.timeout_;
            }
               break;

            case Role::TimeLeft : {
               return r->status_.timeleft_;
            }
               break;

            case Role::BestQPrice : {
               return r->bestQuotedPx_;
            }
               break;

            case Qt::BackgroundRole : {
               switch (index.column()) {
                  case Column::QuotedPx :
                     return r->quotedPriceBrush_;

                  case Column::IndicPx :
                     return r->indicativePxBrush_;

                  case Column::Status :
                     return r->stateBrush_;

                  default :
                     return QVariant();
               }
            }
               break;

            default:
               return QVariant();
         }
      }
         break;

      case DataType::Group : {
         Group *g = static_cast<Group*>(idx->data_);

         switch (role) {
            case Qt::FontRole : {
               if (index.column() == Column::SecurityID) {
                  return g->font_;
               } else {
                  return QVariant();
               }
            }
               break;

            case Qt::DisplayRole : {
               if (index.column() == Column::SecurityID) {
                  return g->security_;
               } else {
                  return QVariant();
               }
            }
               break;

            case Qt::TextColorRole : {
               if (secStatsCollector_) {
                  return secStatsCollector_->getColorFor(g->security_.toStdString());
               } else {
                  return QVariant();
               }
            }
               break;

            case QuoteRequestsModel::Role::Grade : {
               if (secStatsCollector_) {
                  return secStatsCollector_->getGradeFor(g->security_.toStdString());
               } else {
                  return QVariant();
               }
            }
               break;

            default :
               return QVariant();
         }
      }
         break;

      case DataType::Market : {
         Market *m = static_cast<Market*>(idx->data_);

         switch (role) {
            case QuoteRequestsModel::Role::Grade : {
               return 1;
            }
               break;

            case role == Qt::DisplayRole : {
               switch (index.column()) {
                  case Column::SecurityID : {
                     return m->security_;
                  }

                  case Column::Product : {
                     if (m->security_ == groupNameSettlements_ && settlCompleted_) {
                        return QString::number(settlCompleted_);
                     } else {
                        return QVariant();
                     }
                  }
                     break;

                  case Column::Side : {
                     if (m->security_ == groupNameSettlements_ && settlFailed_) {
                        return QString::number(settlFailed_);
                     } else {
                        return QVariant();
                     }
                  }
                     break;

                  default :
                     return QVariant();
               }
            }
               break;

            case Qt::TextColorRole : {
               if (index.column() == Column::Product) {
                  return c_greenColor;
               } else if (index.column() == Column::Side){
                  return c_redColor;
               } else {
                  return QVariant();
               }
            }
               break;


            case Qt::TextAlignmentRole : {
               if (index.column() == Column::Product || index.column() == Column::Side) {
                  return Qt::AlignRight;
               } else {
                  return QVariant();
               }
            }
               break;

            default :
               return QVariant();
         }
      }
         break;

      default :
         return QVariant();
   }
}

QModelIndex QuoteRequestsModel::index(int row, int column, const QModelIndex &parent) const
{
   if (parent.isValid()) {
      IndexHelper *idx = static_cast<IndexHelper*>(parent.internalPointer());

      switch (idx->type_) {
         case DataType::Market : {
            Market *m = static_cast<Market*>(idx->data_);

            if (row < m->groups_.size()) {
               return createIndex(row, column, &m->groups_.at(row)->idx_);
            } else {
               return createIndex(row, column, &m->settl_.rfqs_.at(row - m->groups_.size())->idx_);
            }
         }
            break;

         case DataType::Group : {
            Group *g = static_cast<Group*>(idx->data_);

            return createIndex(row, column, &g->rfqs_.at(row)->idx_);
         }
            break;

         default :
            return QModelIndex();
      }
   } else {
      return createIndex(row, column, &data_.at(row)->idx_);
   }
}

int QuoteRequestsModel::findGroup(IndexHelper *idx)
{
   if (idx->parent_) {
      auto *market = static_cast<Market*>(idx->parent_->data_);

      auto it = std::find_if(market->groups_.cbegin(), market->groups_.cend(),
                   [&idx](const auto &g) { return (&g->idx_ == idx); });

      if (it != market->groups_.cend()) {
         return std::distance(market->groups_.cbegin(), it);
      } else {
         return -1;
      }
   } else if (idx->type_ == DataType::Market) {
      return findMarket(idx);
   } else {
      return -1;
   }
}

QuoteRequestsModel::Group* QuoteRequestsModel::findGroup(Market *market, const QString &security)
{
   auto it = std::find_if(market->groups_.cbegin(), market->groups_.cend(),
      [&] (const auto &g) { return (g->security_ == security); } );

   if (it != market->groups_.cend()) {
      return it->get();
   } else {
      return nullptr;
   }
}

int QuoteRequestsModel::findMarket(IndexHelper *idx)
{
   auto it = std::find_if(data_.cbegin(), data_.cend(),
                [&idx](const auto &m) { return (&m->idx_ == idx); });

   if (it != data_.cend()) {
      return std::distance(data_.cbegin(), it);
   } else {
      return -1;
   }
}

QuoteRequestsModel::Market* QuoteRequestsModel::findMarket(const QString &name)
{
   for (size_t i = 0; i < data_.size(); ++i) {
      if (data_[i]->security_ == name) {
         return data_[i].get();
      }
   }

   return nullptr;
}

QModelIndex QuoteRequestsModel::lastIndex()
{
   if (!data_.empty()) {
      if (!data_.back()->groups_.empty()) {
         if (!data_.back()->groups_.back()->rfqs_.empty()) {
            return createIndex((int) data_.back()->groups_.back()->rfqs_.size() - 1,
               Column::Empty, data_.back()->groups_.back()->rfqs_.back()->idx_);
         } else {
            return createIndex((int) data_.back()->groups_.size() - 1, Column::Empty,
               data_.back()->groups_.back()->idx_);
         }
      } else {
         return createIndex((int) data_.size() - 1, Column::Empty, data_.back()->idx_);
      }
   } else {
      return QModelIndex();
   }
}

QModelIndex QuoteRequestsModel::parent(const QModelIndex &index) const
{
   if (index.isValid()) {
      IndexHelper *idx = static_cast<IndexHelper*>(index.internalPointer());

      if (idx->parent_) {
         switch (idx->parent_->type_) {
            case DataType::Group : {
               return createIndex(findGroup(idx->parent_), 0, idx->parent_);
            }
               break;

            case DataType::Market : {
               return createIndex(findMarket(idx->parent_), 0, idx->parent_);
            }
               break;

            default :
               return QModelIndex();
         }
      } else {
         return QModelIndex();
      }
   } else {
      return QModelIndex();
   }
}

int QuoteRequestsModel::rowCount(const QModelIndex &parent) const
{
   if (parent.isValid()) {
      IndexHelper *idx = static_cast<IndexHelper*>(parent.internalPointer());

      switch (idx->type_) {
         case DataType::Group : {
            return static_cast<int>(static_cast<Group*>(idx->data_)->rfqs_.size());
         }
            break;

         case DataType::Market : {
            auto *market = static_cast<Market*>(idx->data_);
            return static_cast<int>(market->groups_.size() + market->settl_.rfqs_.size());
         }
            break;

         default :
            return 0;
      }
   } else {
      return static_cast<int>(data_.size());
   }
}

QVariant QuoteRequestsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
   if (orientation != Qt::Horizontal) {
      return QVariant();
   }

   if (role != Qt::DisplayRole) {
      return QVariant();
   }

   switch (section) {
      case Column::SecurityID:  return tr("SecurityID");
      case Column::Product:     return tr("Product");
      case Column::Side:        return tr("Side");
      case Column::Quantity:    return tr("Quantity");
      case Column::Party:       return tr("Party");
      case Column::Status:      return tr("Status");
      case Column::QuotedPx:    return tr("Quoted Price");
      case Column::IndicPx:     return tr("Indicative Px");
      case Column::BestPx:      return tr("Best Quoted Px");
      default:                  return QString();
   }
}

void QuoteRequestsModel::SetAssetManager(const std::shared_ptr<AssetManager>& assetManager)
{
   assetManager_ = assetManager;
}

void QuoteRequestsModel::ticker() {
   std::unordered_set<std::string>  deletedRows;
   const auto timeNow = QDateTime::currentDateTime();

   for (const auto &id : pendingDeleteIds_) {
      forSpecificId(id, [](Group *g, int idxItem) {
         beginRemoveRows(createIndex(findGroup(&g->idx_), 0, g->idx_), idxItem, idxItem);
         g->rfqs_.erase(g->rfqs_.begin() + idxItem);
         endRemoveRows();
      });
      deletedRows.insert(id);
   }
   pendingDeleteIds_.clear();

   for (auto qrn : notifications_) {
      const auto timeDiff = timeNow.msecsTo(qrn.second.expirationTime.addMSecs(qrn.second.timeSkewMs));
      if ((timeDiff < 0) || (qrn.second.status == bs::network::QuoteReqNotification::Withdrawn)) {
         forSpecificId(qrn.second.quoteRequestId, [](Group *grp, int itemIndex) {
            const auto row = findGroup(grp->idx_);
            beginRemoveRows(createIndex(row, 0, grp->idx_), itemIndex, itemIndex);
            grp->rfqs_.erase(grp->rfqs_.begin() + itemIndex);
            endRemoveRows();

            if (grp->rfqs_.size() == 0) {
               const auto m = findMarket(grp->idx_.parent_);
               beginRemoveRows(createIndex(m, 0, grp->idx_.parent_), row, row);
               data_[m]->groups_.erase(data_[m]->groups_.begin() + row);
               endRemoveRows();
            }
         });
         deletedRows.insert(qrn.second.quoteRequestId);
      }
      else if ((qrn.second.status == bs::network::QuoteReqNotification::PendingAck)
         || (qrn.second.status == bs::network::QuoteReqNotification::Replied)) {
         forSpecificId(qrn.second.quoteRequestId, [timeDiff](Group *grp, int itemIndex) {
            grp->rfqs_[itemIndex]->status_.timeleft_ = (int) timeDiff;
         });
      }
   }

   for (auto delRow : deletedRows) {
      notifications_.erase(delRow);
   }

   for (const auto &settlContainer : settlContainers_) {
      forSpecificId(settlContainer.second->id(),
         [timeLeft = settlContainer.second->timeLeftMs()](Group *grp, int itemIndex) {
         grp->rfqs_[itemIndex]->status_.timeleft_ = (int) timeLeft;
      });
   }

   if (!data_.empty()) {
      const QModelIndex last = lastIndex();

      emit dataChanged(createIndex(0, Column::Status, &data_[0]->idx_),
         createIndex(last.row(), Column::Status, last.internalPointer()),
         QVector<int>() << Role::TimeLeft);
   }
}

void QuoteRequestsModel::onQuoteNotifCancelled(const QString &reqId)
{
   int row = -1;
   RFQ *rfq = nullptr;

   forSpecificId(reqId.toStdString(), [&](Group *group, int i) {
      row = i;
      rfq = group->rfqs_[i].get();
      rfq->quotedPrice_ = tr("pulled");
   });

   setStatus(reqId.toStdString(), bs::network::QuoteReqNotification::PendingAck);

   if (row >= 0 && rfq) {
      const QModelIndex idx = createIndex(row, Column::QuotedPx, rfq->idx_);
      emit dataChanged(idx, idx, QVector<int>() << Qt::DisplayRole);
   }
}

void QuoteRequestsModel::onQuoteReqCancelled(const QString &reqId, bool byUser)
{
   if (!byUser) {
      return;
   }

   setStatus(reqId.toStdString(), bs::network::QuoteReqNotification::Withdrawn);
}

void QuoteRequestsModel::onQuoteRejected(const QString &reqId, const QString &reason)
{
   setStatus(reqId.toStdString(), bs::network::QuoteReqNotification::Rejected, reason);
}

void QuoteRequestsModel::onBestQuotePrice(const QString reqId, double price, bool own)
{
   int row = -1;
   Group *g = nullptr;

   forSpecificId(reqId.toStdString(), [&](Group *grp, int index) {
      row = index;
      g = grp;
      const auto assetType = grp->rfqs_[index]->assetType_;

      grp->rfqs_[index]->bestQuotedPxString_ = UiUtils::displayPriceForAssetType(price, assetType);
      grp->rfqs_[index]->bestQuotedPx_ = price;
      grp->rfqs_[index]->quotedPriceBrush_ = colorForQuotedPrice(
         grp->rfqs_[index]->quotedPrice_, price, own);
   });

   if (row >= 0 && g) {
      emit dataChanged(createIndex(row, Column::QuotedPx, g->rfqs_[row]->idx_),
         createIndex(row, Column::BestPx, g->rfqs_[row]->idx_),
         QVector<int> () << Qt::DisplayRole << Qt::BackgroundRole);
   }
}

void QuoteRequestsModel::onQuoteReqNotifReplied(const bs::network::QuoteNotification &qn)
{
   int row = -1;
   Group *g = nullptr;

   forSpecificId(qn.quoteRequestId, [&qn](Group *group, int i) {
      row = i;
      g = group;
      const auto assetType = group->rfqs_[i]->assetType_;
      const double quotedPrice = (qn.side == bs::network::Side::Buy) ? qn.bidPx : qn.offerPx;

      group->rfqs_[i]->quotedPriceString_ =
         UiUtils::displayPriceForAssetType(quotedPrice, assetType);
      group->rfqs_[i]->quotedPrice_ = quotedPrice;
      group->rfqs_[i]->quotedPriceBrush_ =
         colorForQuotedPrice(quotedPrice, group->rfqs_[i]->bestQuotedPx_);
   });

   if (row >= 0 && g) {
      const QModelIndex idx = createIndex(row, Column::QuotedPx, g->rfqs_[row]->idx_);
      emit dataChanged(idx, idx,
         QVector<int> () << Qt::DisplayRole << Qt::BackgroundRole);
   }

   setStatus(qn.quoteRequestId, bs::network::QuoteReqNotification::Replied);
}

QBrush QuoteRequestsModel::colorForQuotedPrice(double quotedPrice, double bestQPrice, bool own)
{
   if (qFuzzyIsNull(quotedPrice) || qFuzzyIsNull(bestQPrice)) {
      return {};
   }

   if (own && qFuzzyCompare(quotedPrice, bestQPrice)) {
      return c_greenColor;
   }

   return c_redColor;
}

void QuoteRequestsModel::onQuoteReqNotifReceived(const bs::network::QuoteReqNotification &qrn)
{
   QString marketName = tr(bs::network::Asset::toString(qrn.assetType));
   auto *market = findMarket(marketName);

   if (!market) {
      beginInsertRows(QModelIndex(), data_.size(), data_.size());
      data_.push_back(std::make_unique<Market>(marketName));
      market = data_.back().get();
      endInsertRows();
   }

   QString groupNameSec = QString::fromStdString(qrn.security);
   auto *group = findGroup(market, groupNameSec);

   if (!group) {
      beginInsertRows(createIndex(findMarket(market->idx_), 0, market->idx_),
         market->groups_.size(), market->groups_.size());
      QFont font;
      font.setBold(true);
      market->groups_.push_back(std::make_unique<Group>(groupNameSec, font));
      market->groups_.back()->idx_.parent_ = market;
      group = market->groups_.back().get();
      endInsertRows();
   }

   insertRfq(group, qrn);
}

void QuoteRequestsModel::insertRfq(Group *group, const bs::network::QuoteReqNotification &qrn)
{
   auto itQRN = notifications_.find(qrn.quoteRequestId);

   if (itQRN == notifications_.end()) {
      const auto assetType = assetManager_->GetAssetTypeForSecurity(qrn.security);
      const CurrencyPair cp(qrn.security);
      const bool isBid = (qrn.side == bs::network::Side::Buy) ^ (cp.NumCurrency() == qrn.product);
      const double indicPrice = isBid ? mdPrices_[qrn.security][Role::BidPrice] : mdPrices_[qrn.security][Role::OfferPrice];

      beginInsertRows(createIndex(findGroup(group->idx_), 0, group->idx_),
         group->rfqs_.size(), group->rfqs_.size());

      group->rfqs_.push_back(std::make_unique<RFQ>(QString::fromStdString(qrn.security),
         QString::fromStdString(qrn.product),
         tr(bs::network::Side::toString(qrn.side)),
         QString(),
         (qrn.assetType == bs::network::Asset::Type::PrivateMarket) ?
            UiUtils::displayCCAmount(qrn.quantity) : UiUtils::displayQty(qrn.quantity, qrn.product),
         QString(),
         (!qFuzzyIsNull(indicPrice) ? UiUtils::displayPriceForAssetType(indicPrice, assetType)
            : QString()),
         QString(),
         {
            quoteReqStatusDesc(qrn.status),
            ((qrn.status == bs::network::QuoteReqNotification::Status::PendingAck)
               || (qrn.status == bs::network::QuoteReqNotification::Status::Replied)),
           30000
         },
         indicPrice, 0.0, 0.0,
         assetType,
         qrn.quoteRequestId));

      group->rfqs_.back()->idx_.parent_ = group;

      endInsertRows();

      notifications_[qrn.quoteRequestId] = qrn;
   }
   else {
      setStatus(qrn.quoteRequestId, qrn.status);
   }
}

void QuoteRequestsModel::addSettlementContainer(const std::shared_ptr<bs::SettlementContainer> &container)
{
   settlContainers_[container->id()] = container;
   connect(container.get(), &bs::SettlementContainer::failed,
      this, &QuoteRequestsModel::onSettlementFailed);
   connect(container.get(), &bs::SettlementContainer::completed,
      this, &QuoteRequestsModel::onSettlementCompleted);
   connect(container.get(), &bs::SettlementContainer::timerExpired,
      this, &QuoteRequestsModel::onSettlementExpired);

   auto *market = findMarket(groupNameSettlements_);

   if (!market) {
      beginInsertRows(QModelIndex(), data_.size(), data_.size());
      data_.push_back(std::make_unique<Market>(groupNameSettlements_));
      market = data_.back().get();
      endInsertRows();
   }

   const auto collector = std::make_shared<bs::SettlementStatsCollector>(container);
   const auto assetType = assetManager_->GetAssetTypeForSecurity(container->security());
   const auto amountStr = (container->assetType() == bs::network::Asset::Type::PrivateMarket)
      ? UiUtils::displayCCAmount(container->quantity())
      : UiUtils::displayQty(container->quantity(), container->product());

   beginInsertRows(createIndex(findMarket(market->idx_), 0, market->idx_),
      market->groups_.size() + market->rfqs_.size(),
      market->groups_.size() + market->rfqs_.size());

   market->settl_.rfqs_.push_back(std::make_unique<RFQ>(QString::fromStdString(container->security()),
      QString::fromStdString(container->product()),
      tr(bs::network::Side::toString(container->side())),
      QString(),
      amountStr,
      QString(),
      UiUtils::displayPriceForAssetType(container->price(), assetType),
      QString(),
      {
         QString(),
         true
      },
      container->price(), 0.0, 0.0,
      assetType,
      container->id()));

   market->settl_.rfqs_.back()->idx_.parent_ = market;

   connect(container.get(), &bs::SettlementContainer::timerStarted,
      [s = market->rfqs_.back().get(), &market, this,
       row = market->groups_.size() + market->rfqs_.size() - 1](int msDuration) {
         s->status_.timeout_ = msDuration;
         const QModelIndex idx = createIndex(row, 0, s->idx_);
         emit dataChanged(idx, idx, QVector<int> () << Role::Timeout);
      }
   );

   endInsertRows();
}


void QuoteRequestsModel::onSettlementCompleted()
{
   ++settlCompleted_;
   deleteSettlement(qobject_cast<bs::SettlementContainer *>(sender()));
}

void QuoteRequestsModel::onSettlementFailed()
{
   ++settlFailed_;
   deleteSettlement(qobject_cast<bs::SettlementContainer *>(sender()));
}

void QuoteRequestsModel::onSettlementExpired()
{
   deleteSettlement(qobject_cast<bs::SettlementContainer *>(sender()));
}

void QuoteRequestsModel::deleteSettlement(bs::SettlementContainer *container)
{
   updateSettlementCounters();
   if (container) {
      pendingDeleteIds_.insert(container->id());
      container->deactivate();
      settlContainers_.erase(container->id());
   }
}

void QuoteRequestsModel::updateSettlementCounters()
{
   auto * market = findMarket(groupNameSettlements_);

   if (market) {
      const int row = findMarket(market->idx_);

      emit dataChanged(createIndex(row, Column::Product, market->idx_),
         createIndex(row, Column::Side, market->idx_));
   }
}

void QuoteRequestsModel::forSpecificId(const std::string &reqId, const cbItem &cb)
{
   for (size_t i = 0; i < data_.size(); ++i) {
      if (!data_[i]->settl_.rfqs_.empty()) {
         for (size_t k = 0; k < data_[i]->settl_.rfqs_.size(); ++k) {
            if (data_[i]->settl_.rfqs_[k]->reqId_ == reqId) {
               cb(&data_[i]->settl_, k);
               return;
            }
         }
      }

      for (size_t j = 0; j < data_[i]->groups_.size(); ++j) {
         for (size_t k = 0; k < data_[i]->groups_[j]->rfqs_.size(); ++k) {
            if (data_[i]->groups_[j]->rfqs_[k]->reqId_ == reqId) {
               cb(data_[i]->groups_[j].get(), k);
               return;
            }
         }
      }
   }
}

void QuoteRequestsModel::forEachSecurity(const QString &security, const cbItem &cb)
{
   for (size_t i = 0; i < data_.size(); ++i) {
      for (int j = 0; j < data_[i]->groups_.size(); ++j) {
         if (data_[i]->groups_[j]->security_ != security)
            continue;
         for (int k = 0; k < data_[i]->groups_[j]->rfqs_.size(); ++k) {
            cb(data_[i]->groups_[j].get(), k);
         }
      }
   }
}

const bs::network::QuoteReqNotification &QuoteRequestsModel::getQuoteReqNotification(const std::string &id) const
{
   static bs::network::QuoteReqNotification   emptyQRN;

   auto itQRN = notifications_.find(id);
   if (itQRN == notifications_.end())  return emptyQRN;
   return itQRN->second;
}

double QuoteRequestsModel::getPrice(const std::string &security, Role::Index role) const
{
   const auto itMDP = mdPrices_.find(security);
   if (itMDP != mdPrices_.end()) {
      const auto itP = itMDP->second.find(role);
      if (itP != itMDP->second.end()) {
         return itP->second;
      }
   }
   return 0;
}

QString QuoteRequestsModel::quoteReqStatusDesc(bs::network::QuoteReqNotification::Status status)
{
   switch (status) {
      case bs::network::QuoteReqNotification::Withdrawn:    return tr("Withdrawn");
      case bs::network::QuoteReqNotification::PendingAck:   return tr("PendingAck");
      case bs::network::QuoteReqNotification::Replied:      return tr("Replied");
      case bs::network::QuoteReqNotification::TimedOut:     return tr("TimedOut");
      case bs::network::QuoteReqNotification::Rejected:     return tr("Rejected");
      default:       return QString();
   }
}

QBrush QuoteRequestsModel::bgColorForStatus(bs::network::QuoteReqNotification::Status status)
{
   switch (status) {
      case bs::network::QuoteReqNotification::Withdrawn:    return Qt::magenta;
      case bs::network::QuoteReqNotification::Rejected:     return c_redColor;
      case bs::network::QuoteReqNotification::Replied:      return c_greenColor;
      case bs::network::QuoteReqNotification::TimedOut:     return c_yellowColor;
      case bs::network::QuoteReqNotification::PendingAck:
      default:       return QBrush();
   }
}

void QuoteRequestsModel::setStatus(const std::string &reqId, bs::network::QuoteReqNotification::Status status
   , const QString &details)
{
   auto itQRN = notifications_.find(reqId);
   if (itQRN != notifications_.end()) {
      itQRN->second.status = status;

      forSpecificId(reqId, [status, details](Group *grp, int index) {
         if (!details.isEmpty()) {
            grp->rfqs_[index]->status_.status_ = details;
         }
         else {
            grp->rfqs_[index]->status_.status_ = quoteReqStatusDesc(status);
         }

         grp->rfqs_[index]->stateBrush_ = bgColorForStatus(status);

         const bool showProgress = ((status == bs::network::QuoteReqNotification::Status::PendingAck)
            || (status == bs::network::QuoteReqNotification::Status::Replied));
         grp->rfqs_[index]->status_.showProgress_ = showProgress;

         const QModelIndex idx = createIndex(index, Column::Status, &grp->rfqs_[index]->idx_);
         emit dataChanged(idx, idx);
      });

      emit quoteReqNotifStatusChanged(itQRN->second);
   }
}

void QuoteRequestsModel::onSecurityMDUpdated(const QString &security, const bs::network::MDFields &mdFields)
{
   const auto pxBid = bs::network::MDField::get(mdFields, bs::network::MDField::PriceBid);
   const auto pxOffer = bs::network::MDField::get(mdFields, bs::network::MDField::PriceOffer);
   if (pxBid.type != bs::network::MDField::Unknown) {
      mdPrices_[security.toStdString()][Role::BidPrice] = pxBid.value;
   }
   if (pxOffer.type != bs::network::MDField::Unknown) {
      mdPrices_[security.toStdString()][Role::OfferPrice] = pxOffer.value;
   }

   forEachSecurity(security, [security, pxBid, pxOffer, this](Group *grp, int index) {
      const CurrencyPair cp(security.toStdString());
      const bool isBuy = (static_cast<bs::network::Side::Type>(grp->child(index, Header::Side)->data(Role::Side).toInt()) == bs::network::Side::Buy)
         ^ (cp.NumCurrency() == grp->child(index, Header::first)->data(Role::Product).toString().toStdString());
      double indicPrice = 0;

      if (isBuy && (pxBid.type != bs::network::MDField::Unknown)) {
         indicPrice = pxBid.value;
      } else if (!isBuy && (pxOffer.type != bs::network::MDField::Unknown)) {
         indicPrice = pxOffer.value;
      }

      if (indicPrice > 0) {
         const auto prevPrice = grp->rfqs_[index]->indicativePx_;
         const auto assetType = grp->rfqs_[index]->assetType_;
         grp->rfqs_[index]->indicativePxString_ = UiUtils::displayPriceForAssetType(indicPrice, assetType);
         grp->rfqs_[index]->indicativePx_ = indicPrice;

         if (!qFuzzyIsNull(prevPrice)) {
            if (indicPrice > prevPrice) {
               grp->rfqs_[index]->indicativePxBrush_ = c_greenColor;
            }
            else if (indicPrice < prevPrice) {
               grp->rfqs_[index]->indicativePxBrush_ = c_redColor;
            }
         }

         const QModelIndex idx = createIndex(index, Column::IndicPx, &grp->rfqs_[index]->idx_);
         emit dataChanged(idx, idx);
      }
   });
}
