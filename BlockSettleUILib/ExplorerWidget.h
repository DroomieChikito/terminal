#ifndef __EXPLORERWIDGET_H__
#define __EXPLORERWIDGET_H__

#include <QWidget>
#include <memory>
#include "TabWithShortcut.h"
#include "ArmoryConnection.h"

namespace Ui {
   class ExplorerWidget;
}

//class QStringListModel;

class ExplorerWidget : public TabWithShortcut
{
Q_OBJECT

public:
    ExplorerWidget(QWidget *parent = nullptr);
    ~ExplorerWidget() override;

   void init(const std::shared_ptr<ArmoryConnection> &);
   void shortcutActivated(ShortcutType s) override;

protected slots:
   void onSearchStarted();

private:
   std::unique_ptr<Ui::ExplorerWidget> ui_;
   std::shared_ptr<ArmoryConnection>   armory_;
};

#endif // EXPLORERWIDGET_H
