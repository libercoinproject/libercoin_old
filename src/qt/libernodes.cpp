#include "libernodes.h"
#include "ui_libernodes.h"

#include "activelibernode.h"
#include "clientmodel.h"
#include "init.h"
#include "guiutil.h"
#include "libernode-sync.h"
#include "libernodeconfig.h"
#include "libernodeman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "utiltime.h"

#include <QTimer>
#include <QMessageBox>
#include <QGraphicsDropShadowEffect>
#include <QLinearGradient>


int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}


Libernodes::Libernodes(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Libernodes),
    clientModel(0),
    walletModel(0)
{

    ui->setupUi(this);

    statusBar = ui->statusBar;
    statusText = ui->statusText;
    priceUSD = ui->priceUSD;
    priceBTC = ui->priceBTC;
    //ui->startButton_4->setEnabled(false);

    int columnAliasWidth = 80;
    int columnAddressWidth = 80;
    int columnProtocolWidth = 80;
    int columnStatusWidth = 80;
    int columnActiveWidth = 80;
    int columnLastSeenWidth = 80;

    ui->tableWidgetMyLibernodes_4->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyLibernodes_4->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyLibernodes_4->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMyLibernodes_4->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMyLibernodes_4->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMyLibernodes_4->setColumnWidth(5, columnLastSeenWidth);
    ui->tableWidgetMyLibernodes_4->horizontalHeader()->setFixedHeight(50);
    ui->tableWidgetLibernodes_4->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetLibernodes_4->setColumnWidth(1, columnProtocolWidth);
    ui->tableWidgetLibernodes_4->setColumnWidth(2, columnStatusWidth);
    ui->tableWidgetLibernodes_4->setColumnWidth(3, columnActiveWidth);
    ui->tableWidgetLibernodes_4->setColumnWidth(4, columnLastSeenWidth);

    //ui->tableWidgetMyLibernodes_4->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->tableWidgetLibernodes_4->horizontalHeader()->setFixedHeight(50);

    //connect(ui->tableWidgetMyLibernodes_4, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(ui->startButton_4, SIGNAL(clicked()), this, SLOT(on_startButton_clicked()));
    connect(ui->startAllButton_4, SIGNAL(clicked()), this, SLOT(on_startAllButton_clicked()));
    connect(ui->startMissingButton_4, SIGNAL(clicked()), this, SLOT(on_startMissingButton_clicked()));
    connect(ui->UpdateButton_4, SIGNAL(clicked()), this, SLOT(on_UpdateButton_clicked()));
    connect(ui->swapButton, SIGNAL(triggered()), this, SLOT(on_swapButton_clicked()));
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    fFilterUpdated = false;
    nTimeFilterUpdated = GetTime();
    updateNodeList();


    QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect();
    effect->setOffset(0);
    effect->setBlurRadius(20.0);

    ui->frame_2->setGraphicsEffect(effect);

    ui->autoupdate_label_4->setVisible(true);
    ui->secondsLabel_4->setVisible(true);
    ui->label->setVisible(true);
    ui->tabLabel->setText("My Libernodes ");
    ui->swapButton->setText("All Libernodes ");
    ui->label_count_4->setText("My Node Count: ");
    ui->countLabel_4->setText(QString::number(ui->tableWidgetMyLibernodes_4->rowCount()));
    ui->tabWidget->setCurrentIndex(0);
}


Libernodes::~Libernodes()
{
    delete ui;
}



void Libernodes::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when libernode count changes
        connect(clientModel, SIGNAL(strLibernodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void Libernodes::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
}

void Libernodes::showContextMenu(const QPoint &point)
{
    QTableWidgetItem *item = ui->tableWidgetMyLibernodes_4->itemAt(point);
    if(item) contextMenu->exec(QCursor::pos());
}

void Libernodes::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;

    BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry mne, libernodeConfig.getEntries()) {
        if(mne.getAlias() == strAlias) {
            std::string strError;
            CLibernodeBroadcast mnb;

            bool fSuccess = CLibernodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

            if(fSuccess) {
                strStatusHtml += "<br>Successfully started libernode.";
                mnodeman.UpdateLibernodeList(mnb);
                mnb.RelayLiberNode();
                mnodeman.NotifyLibernodeUpdates();
            } else {
                strStatusHtml += "<br>Failed to start libernode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void Libernodes::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry mne, libernodeConfig.getEntries()) {
        std::string strError;
        CLibernodeBroadcast mnb;

        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        COutPoint outpoint = COutPoint(uint256S(mne.getTxHash()), nOutputIndex);

        if(strCommand == "start-missing" && mnodeman.Has(CTxIn(outpoint))) continue;

        bool fSuccess = CLibernodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb);

        if(fSuccess) {
            nCountSuccessful++;
            mnodeman.UpdateLibernodeList(mnb);
            mnb.RelayLiberNode();
            mnodeman.NotifyLibernodeUpdates();
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d libernodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void Libernodes::updateMyLibernodeInfo(QString strAlias, QString strAddr, const COutPoint& outpoint)
{
    bool fOldRowFound = false;
    int nNewRow = 0;

    for(int i = 0; i < ui->tableWidgetMyLibernodes_4->rowCount(); i++) {
        if(ui->tableWidgetMyLibernodes_4->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if(nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyLibernodes_4->rowCount();
        ui->tableWidgetMyLibernodes_4->insertRow(nNewRow);
    }

    libernode_info_t infoMn = mnodeman.GetLibernodeInfo(CTxIn(outpoint));
    bool fFound = infoMn.fInfoValid;

    QTableWidgetItem *aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem *addrItem = new QTableWidgetItem(fFound ? QString::fromStdString(infoMn.addr.ToString()) : strAddr);
    QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(fFound ? infoMn.nProtocolVersion : -1));
    QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(fFound ? CLibernode::StateToString(infoMn.nActiveState) : "MISSING"));
    QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(fFound ? (infoMn.nTimeLastPing - infoMn.sigTime) : 0)));
    QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M",
                                                                                                   fFound ? infoMn.nTimeLastPing + GetOffsetFromUtc() : 0)));
    QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(fFound ? CBitcoinAddress(infoMn.pubKeyCollateralAddress.GetID()).ToString() : ""));
    QLinearGradient grad1(50,50,50,50);
    grad1.setColorAt(0,QColor(0,0,0,0));
    grad1.setColorAt(0,QColor(255,255,255));
    QBrush brush(grad1);
    aliasItem->setBackground(brush);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 2, protocolItem);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 3, statusItem);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 4, activeSecondsItem);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 5, lastSeenItem);
    ui->tableWidgetMyLibernodes_4->setItem(nNewRow, 6, pubkeyItem);
    //ui->tableWidgetMyLibernodes_4->setColumnWidth(0, 100);

}

void Libernodes::updateMyNodeList(bool fForce)
{
    TRY_LOCK(cs_mymnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my libernode list only once in MY_MASTERNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_MASTERNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel_4->setText(QString::number(nSecondsTillUpdate) + "s");

    if(nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetLibernodes_4->setSortingEnabled(false);
    BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry mne, libernodeConfig.getEntries()) {
        int32_t nOutputIndex = 0;
        if(!ParseInt32(mne.getOutputIndex(), &nOutputIndex)) {
            continue;
        }

        updateMyLibernodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), COutPoint(uint256S(mne.getTxHash()), nOutputIndex));
    }
    ui->tableWidgetLibernodes_4->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel_4->setText("0");
}

void Libernodes::updateNodeList()
{
    TRY_LOCK(cs_mnlist, fLockAcquired);
    if(!fLockAcquired) {
        return;
    }

    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in MASTERNODELIST_UPDATE_SECONDS seconds
    // or MASTERNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + MASTERNODELIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + MASTERNODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel_4->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    QString strToFilter;
    ui->countLabel_4->setText("Updating...");
    ui->tableWidgetLibernodes_4->setSortingEnabled(false);
    ui->tableWidgetLibernodes_4->clearContents();
    ui->tableWidgetLibernodes_4->setRowCount(0);
//    std::map<COutPoint, CLibernode> mapLibernodes = mnodeman.GetFullLibernodeMap();
    std::vector<CLibernode> vLibernodes = mnodeman.GetFullLibernodeVector();
    int offsetFromUtc = GetOffsetFromUtc();

    BOOST_FOREACH(CLibernode & mn, vLibernodes)
    {
//        CLibernode mn = mnpair.second;
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));
        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(mn.nProtocolVersion));
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(mn.GetStatus()));
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(mn.lastPing.sigTime - mn.sigTime)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime + offsetFromUtc)));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetLibernodes_4->insertRow(0);
        ui->tableWidgetLibernodes_4->setItem(0, 0, addressItem);
        ui->tableWidgetLibernodes_4->setItem(0, 1, protocolItem);
        ui->tableWidgetLibernodes_4->setItem(0, 2, statusItem);
        ui->tableWidgetLibernodes_4->setItem(0, 3, activeSecondsItem);
        ui->tableWidgetLibernodes_4->setItem(0, 4, lastSeenItem);
        ui->tableWidgetLibernodes_4->setItem(0, 5, pubkeyItem);
    }

    if(ui->tabWidget->currentIndex()==0){
        ui->label_count_4->setText("My Node Count: ");
        ui->countLabel_4->setText(QString::number(ui->tableWidgetMyLibernodes_4->rowCount()));
    }
    else if(ui->tabWidget->currentIndex()==1){
        ui->label_count_4->setText("Total Node Count: ");
        ui->countLabel_4->setText(QString::number(ui->tableWidgetLibernodes_4->rowCount()));
    }
    ui->tableWidgetLibernodes_4->setSortingEnabled(true);
}

void Libernodes::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel_4->setText(QString::fromStdString(strprintf("Please wait... %d", MASTERNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void Libernodes::on_startButton_clicked()
{
    std::cout << "START" << std::endl;
    std::string strAlias;
    {
        LOCK(cs_mymnlist);
        // Find selected node alias
        QItemSelectionModel* selectionModel = ui->tableWidgetMyLibernodes_4->selectionModel();
        QModelIndexList selected = selectionModel->selectedRows();

        if(selected.count() == 0) return;

        QModelIndex index = selected.at(0);
        int nSelectedRow = index.row();
        strAlias = ui->tableWidgetMyLibernodes_4->item(nSelectedRow, 0)->text().toStdString();
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm libernode start"),
        tr("Are you sure you want to start libernode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void Libernodes::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all libernodes start"),
        tr("Are you sure you want to start ALL libernodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void Libernodes::on_startMissingButton_clicked()
{

    if(!libernodeSync.IsLibernodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until libernode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing libernodes start"),
        tr("Are you sure you want to start MISSING libernodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if(encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForMixingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if(!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void Libernodes::on_tableWidgetMyLibernodes_itemSelectionChanged()
{
    if(ui->tableWidgetMyLibernodes_4->selectedItems().count() > 0) {
        ui->startButton_4->setEnabled(true);
    }
}

void Libernodes::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}

void Libernodes::on_swapButton_clicked()
{
    if(ui->tabWidget->currentIndex()==0){
        ui->autoupdate_label_4->setVisible(false);
        ui->secondsLabel_4->setVisible(false);
        ui->label->setVisible(false);
        ui->tabLabel->setText("All Libernodes ");
        ui->swapButton->setText("My Libernodes ");
        ui->label_count_4->setText("Total Node Count: ");
        ui->countLabel_4->setText(QString::number(ui->tableWidgetLibernodes_4->rowCount()));
        ui->tabWidget->setCurrentIndex(1);
        return;
    }
    else if(ui->tabWidget->currentIndex()==1){
        ui->autoupdate_label_4->setVisible(true);
        ui->secondsLabel_4->setVisible(true);
        ui->label->setVisible(true);
        ui->tabLabel->setText("My Libernodes ");
        ui->swapButton->setText("All Libernodes ");
        ui->label_count_4->setText("My Node Count: ");
        ui->countLabel_4->setText(QString::number(ui->tableWidgetMyLibernodes_4->rowCount()));
        ui->tabWidget->setCurrentIndex(0);
        return;
    }
}

