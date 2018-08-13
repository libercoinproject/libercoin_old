// Copyright (c) 2017 The Libercoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "communitypage.h"
#include "ui_communitypage.h"
#include "clientmodel.h"
#include "walletmodel.h"

#include "guiutil.h"
#include "guiconstants.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QGraphicsDropShadowEffect>
#include <QDesktopServices>
#include <QUrl>
#include <QWidget>

CommunityPage::CommunityPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CommunityPage)
{

    ui->setupUi(this);
    connect(ui->website, SIGNAL(pressed()), this, SLOT(OpenWebsite()));
    connect(ui->reddit, SIGNAL(pressed()), this, SLOT(OpenReddit()));
    connect(ui->btcTalk, SIGNAL(pressed()), this, SLOT(OpenBTCTalk()));
    connect(ui->twitter, SIGNAL(pressed()), this, SLOT(OpenTwitter()));
    connect(ui->slack, SIGNAL(pressed()), this, SLOT(OpenSlack()));
    connect(ui->facebook, SIGNAL(pressed()), this, SLOT(OpenFacebook()));
    connect(ui->slackInv, SIGNAL(pressed()), this, SLOT(OpenSlackInv()));
    connect(ui->git, SIGNAL(pressed()), this, SLOT(OpenGit()));

}


CommunityPage::~CommunityPage()
{
    delete ui;
}


void CommunityPage::OpenWebsite(){
    QString website = "http://www.liberco.in/";
    QDesktopServices::openUrl(QUrl(website));
}
void CommunityPage::OpenReddit(){
    QString reddit = "https://www.reddit.com/r/libercoin/";
    QDesktopServices::openUrl(QUrl(reddit));
}
void CommunityPage::OpenBTCTalk(){
    QString btc = "https://bitcointalk.org/index.php";
    QDesktopServices::openUrl(QUrl(btc));
}
void CommunityPage::OpenTwitter(){
    QString twitter = "https://twitter.com/libercoinofficial";
    QDesktopServices::openUrl(QUrl(twitter));
}
void CommunityPage::OpenSlack(){
    QString slack = "https://discord.gg/mE3KemF";
    QDesktopServices::openUrl(QUrl(slack));
}
void CommunityPage::OpenFacebook(){
    QString fb = "https://www.facebook.com/libercoin";
    QDesktopServices::openUrl(QUrl(fb));
}
void CommunityPage::OpenSlackInv(){
    QString slackInv = "https://t.me/libercoinofficial";
    QDesktopServices::openUrl(QUrl(slackInv));
}
void CommunityPage::OpenGit(){
    QString git = "https://github.com/libercoinofficial/libercoin";
    QDesktopServices::openUrl(QUrl(git));
}

