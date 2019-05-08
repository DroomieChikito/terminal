#ifndef __PULL_OWN_OTC_REQUEST_WIDGET_H__
#define __PULL_OWN_OTC_REQUEST_WIDGET_H__

#include <QWidget>

#include "CommonTypes.h"

namespace Ui {
    class PullOwnOTCRequestWidget;
};

class PullOwnOTCRequestWidget : public QWidget
{
Q_OBJECT

public:
   PullOwnOTCRequestWidget(QWidget* parent = nullptr );
   ~PullOwnOTCRequestWidget() noexcept override;

   void DisplayActiveOTC(const bs::network::LiveOTCRequest& otc);
   void DisplaySubmittedOTC(const bs::network::OTCRequest& otc);

private slots:
   void OnPullPressed();

signals:
   void PullOTCRequested(const std::string& otcId);

private:
   std::unique_ptr<Ui::PullOwnOTCRequestWidget> ui_;

   std::string currentOtcId_;
};

#endif // __PULL_OWN_OTC_REQUEST_WIDGET_H__