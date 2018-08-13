// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef RECEIVECOINSDIALOG_H
#define RECEIVECOINSDIALOG_H



#include <QWidget>
#include <QDialog>
#include <QBoxLayout>
#include <QLabel>
#include "platformstyle.h"

class AddressTableModel;
class OptionsModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QTableView;
class QItemSelection;
class QSortFilterProxyModel;
class QMenu;
class QModelIndex;
QT_END_NAMESPACE

namespace Ui {
    class ReceiveCoinsDialog;
}

class ReceiveCoinsDialog : public QWidget
{
    Q_OBJECT
public:
    explicit ReceiveCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);

    enum Tabs {
        SendingTab = 0,
        ReceivingTab = 1,
        ZerocoinTab = 2
    };

    enum Mode {
        ForSending, /**< Open address book to pick address for sending */
        ForEditing  /**< Open address book for editing */
    };

    ~ReceiveCoinsDialog();

    void setModel(AddressTableModel *model);
    void setOptionsModel(OptionsModel *optionsModel);
    void setWalletModel(WalletModel *walletModel);
    const QString &getReturnValue() const { return returnValue; }
    void done(int);
    QHBoxLayout *statusBar;
    QVBoxLayout *statusText;
    QLabel *priceBTC;
    QLabel *priceUSD;


private:
    Ui::ReceiveCoinsDialog *ui;
    AddressTableModel *model;
    WalletModel *walletModel;
    OptionsModel *optionsModel;
    Mode mode;
    Tabs tab;
    QString returnValue;
    QSortFilterProxyModel *proxyModel;
    QMenu *contextMenu;
    QAction *deleteAction; // to be able to explicitly disable it
    QString newAddressToSelect;


private Q_SLOTS:
    /** Create a new address for receiving coins and / or add a new address book entry */
    void on_newAddress_clicked();
    /** Copy address of currently selected address entry to clipboard */
    void on_copyAddress_clicked();
    /** Open the sign message tab in the Sign/Verify Message dialog with currently selected address */
    void on_signMessage_clicked();
    /** Open send coins dialog for currently selected address (no button) */
    void onSendCoinsAction();
    /** Generate a QR Code from the currently selected address */
    void on_showQRCode_clicked();
    /** Copy label of currently selected address entry to clipboard (no button) */
    void onCopyLabelAction();
    /** Edit currently selected address entry (no button) */
    void onEditAction();
    /** Export button clicked */
    void on_exportButton_clicked();
    /** Show private paper wallet */
    void on_showPrivatePaperWallet_clicked();
    void on_showImportPrivateKey_clicked();


    /** Set button states based on selected tab and selection */
    void selectionChanged();
    /** Spawn contextual menu (right mouse menu) for address book entry */
    void contextualMenu(const QPoint &point);
    /** New entry/entries were added to address table */
    void selectNewAddress(const QModelIndex &parent, int begin, int /*end*/);

Q_SIGNALS:
    void signMessage(QString addr);
    void verifyMessage(QString addr);
    void sendCoins(QString addr);

};


#endif
