// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_WALLETVIEW_H
#define BITCOIN_QT_WALLETVIEW_H

#include "amount.h"
#include "libernodes.h"
#include "votingpage.h"
#include <QStackedWidget>
#include <QProgressBar>
#include <QMenuBar>
#include <QtWidgets>
#include <QNetworkAccessManager>

class CommunityPage;
class LearnMorePage;
class BitcoinGUI;
class ClientModel;
class OverviewPage;
class PlatformStyle;
class ReceiveCoinsDialog;
class SendCoinsDialog;
class SendCoinsRecipient;
class TransactionView;
class WalletModel;
class AddressBookPage;
class ZerocoinPage;
class ReceiveCoinsPage;
class AddressBookPage;
QT_BEGIN_NAMESPACE
class QModelIndex;
class QProgressDialog;
QT_END_NAMESPACE



static int currentCurrency;
/*
  WalletView class. This class represents the view to a single wallet.
  It was added to support multiple wallet functionality. Each wallet gets its own WalletView instance.
  It communicates with both the client and the wallet models to give the user an up-to-date view of the
  current core state.
*/
class WalletView : public QStackedWidget
{
    Q_OBJECT
    QNetworkAccessManager* nam;

public:
    explicit WalletView(const PlatformStyle *platformStyle, QWidget *parent);
    ~WalletView();



    void setBitcoinGUI(BitcoinGUI *gui);
    /** Set the client model.
        The client model represents the part of the core that communicates with the P2P network, and is wallet-agnostic.
    */
    void setClientModel(ClientModel *clientModel);
    /** Set the wallet model.
        The wallet model represents a bitcoin wallet, and offers access to the list of transactions, address book and sending
        functionality.
    */
    void setWalletModel(WalletModel *walletModel);

    bool handlePaymentRequest(const SendCoinsRecipient& recipient);

    void showOutOfSyncWarning(bool fShow);

protected:
    void timerEvent(QTimerEvent *event);
    int timerId;
private:
    ClientModel *clientModel;
    WalletModel *walletModel;
    OverviewPage *overviewPage;
    CommunityPage *communityPage;
    LearnMorePage *learnMorePage;
    QWidget *transactionsPage;
    ReceiveCoinsDialog *receiveCoinsPage;
    AddressBookPage *addressBookPage;
    SendCoinsDialog *sendCoinsPage;
    Libernodes *libernodePage;
    VotingPage *votingPage;

    //AddressBookPage *usedSendingAddressesPage;
    //AddressBookPage *usedReceivingAddressesPage;
    ZerocoinPage *zerocoinPage;
    TransactionView *transactionView;
    QProgressDialog *progressDialog;
    const PlatformStyle *platformStyle;

    QLabel *labelEncryptionIcon;
    QLabel *labelConnectionsIcon;
    QLabel *labelBlocksIcon;
    QLabel *progressBarLabel;
    QProgressBar *progressBar;
    QHBoxLayout *statusBar;
    QVBoxLayout *statusText;
    BitcoinGUI *gui;

public Q_SLOTS:
    void gotoLibernodePage();
    /** Switch to community (social) page */
    void gotoCommunityPage();
    /** Switch to learn more page */
    void gotoLearnMorePage();
    /** Switch to overview (home) page */
    void gotoOverviewPage();
    /** Switch to history (transactions) page */
    void gotoHistoryPage();
    /** Switch to receive coins page */
    void gotoReceiveCoinsPage();
    /** Switch to send coins page */
    void gotoSendCoinsPage(QString addr = "", QString name = "");
    /** Switch to zerocoin page */
    void gotoZerocoinPage();
    /** Switch to address book page */
    void gotoAddressBookPage();
    /** Show Sign/Verify Message dialog and switch to sign message tab */
    void gotoSignMessageTab(QString addr = "");
    /** Show Sign/Verify Message dialog and switch to verify message tab */
    void gotoVerifyMessageTab(QString addr = "");
    void gotoVotingPage();
    /** Show incoming transaction notification for new transactions.

        The new items are those between start and end inclusive, under the given parent item.
    */
    void processNewTransaction(const QModelIndex& parent, int start, int /*end*/);
    /** Encrypt the wallet */
    void encryptWallet(bool status);
    /** Backup the wallet */
    void backupWallet();
    /** Change encrypted wallet passphrase */
    void changePassphrase();
    /** Ask for passphrase to unlock wallet temporarily */
    void unlockWallet();

    /** Show used sending addresses */
    void usedSendingAddresses();
    /** Show used receiving addresses */
    void usedReceivingAddresses();

    /** Re-emit encryption status signal */
    void updateEncryptionStatus();

    /** Show progress dialog e.g. for rescan */
    void showProgress(const QString &title, int nProgress);

    void fetchPrice();
    void replyFinished(QNetworkReply *reply);

Q_SIGNALS:
    /** Signal that we want to show the main window */
    void showNormalIfMinimized();
    /**  Fired when a message should be reported to the user */
    void message(const QString &title, const QString &message, unsigned int style);
    /** Encryption status of wallet changed */
    void encryptionStatusChanged(int status);
    /** Notify that a new transaction appeared */
    void incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address, const QString& label);
};

#endif // BITCOIN_QT_WALLETVIEW_H
