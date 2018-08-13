// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef LIBERNODE_SYNC_H
#define LIBERNODE_SYNC_H

#include "chain.h"
#include "net.h"
#include  "utiltime.h"
#include <univalue.h>

class CLibernodeSync;

static const int LIBERNODE_SYNC_FAILED          = -1;
static const int LIBERNODE_SYNC_INITIAL         = 0;
static const int LIBERNODE_SYNC_SPORKS          = 1;
static const int LIBERNODE_SYNC_LIST            = 2;
static const int LIBERNODE_SYNC_MNW             = 3;
static const int LIBERNODE_SYNC_FINISHED        = 999;

static const int LIBERNODE_SYNC_TICK_SECONDS    = 6;
static const int LIBERNODE_SYNC_TIMEOUT_SECONDS = 30; // our blocks are 2.5 minutes so 30 seconds should be fine

static const int LIBERNODE_SYNC_ENOUGH_PEERS    = 6;  //Mainnet PARAMS
//static const int LIBERNODE_SYNC_ENOUGH_PEERS    = 1;  //Testnet PARAMS

extern CLibernodeSync libernodeSync;

//
// CLibernodeSync : Sync libernode assets in stages
//

class CLibernodeSync
{
private:
    // Keep track of current asset
    int nRequestedLibernodeAssets;
    // Count peers we've requested the asset from
    int nRequestedLibernodeAttempt;

    // Time when current libernode asset sync started
    int64_t nTimeAssetSyncStarted;

    // Last time when we received some libernode asset ...
    int64_t nTimeLastLibernodeList;
    int64_t nTimeLastPaymentVote;
    int64_t nTimeLastGovernanceItem;
    // ... or failed
    int64_t nTimeLastFailure;

    // How many times we failed
    int nCountFailures;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    bool CheckNodeHeight(CNode* pnode, bool fDisconnectStuckNodes = false);
    void Fail();
    void ClearFulfilledRequests();

public:
    CLibernodeSync() { Reset(); }

    void AddedLibernodeList() { nTimeLastLibernodeList = GetTime(); }
    void AddedPaymentVote() { nTimeLastPaymentVote = GetTime(); }
    void AddedGovernanceItem() { nTimeLastGovernanceItem = GetTime(); }

    void SendGovernanceSyncRequest(CNode* pnode);

    bool IsFailed() { return nRequestedLibernodeAssets == LIBERNODE_SYNC_FAILED; }
    bool IsBlockchainSynced(bool fBlockAccepted = false);
    bool IsLibernodeListSynced() { return nRequestedLibernodeAssets > LIBERNODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return nRequestedLibernodeAssets > LIBERNODE_SYNC_MNW; }
    bool IsSynced() { return nRequestedLibernodeAssets == LIBERNODE_SYNC_FINISHED; }

    int GetAssetID() { return nRequestedLibernodeAssets; }
    int GetAttempt() { return nRequestedLibernodeAttempt; }
    std::string GetAssetName();
    std::string GetSyncStatus();

    void Reset();
    void SwitchToNextAsset();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    void ProcessTick();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
