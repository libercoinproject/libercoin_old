// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activelibernode.h"
#include "checkpoints.h"
#include "main.h"
#include "libernode.h"
#include "libernode-payments.h"
#include "libernode-sync.h"
#include "libernodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

class CLibernodeSync;

CLibernodeSync libernodeSync;

bool CLibernodeSync::CheckNodeHeight(CNode *pnode, bool fDisconnectStuckNodes) {
    CNodeStateStats stats;
    if (!GetNodeStateStats(pnode->id, stats) || stats.nCommonHeight == -1 || stats.nSyncHeight == -1) return false; // not enough info about this peer

    // Check blocks and headers, allow a small error margin of 1 block
    if (pCurrentBlockIndex->nHeight - 1 > stats.nCommonHeight) {
        // This peer probably stuck, don't sync any additional data from it
        if (fDisconnectStuckNodes) {
            // Disconnect to free this connection slot for another peer.
            pnode->fDisconnect = true;
            LogPrintf("CLibernodeSync::CheckNodeHeight -- disconnecting from stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                      pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        } else {
            LogPrintf("CLibernodeSync::CheckNodeHeight -- skipping stuck peer, nHeight=%d, nCommonHeight=%d, peer=%d\n",
                      pCurrentBlockIndex->nHeight, stats.nCommonHeight, pnode->id);
        }
        return false;
    } else if (pCurrentBlockIndex->nHeight < stats.nSyncHeight - 1) {
        // This peer announced more headers than we have blocks currently
        LogPrintf("CLibernodeSync::CheckNodeHeight -- skipping peer, who announced more headers than we have blocks currently, nHeight=%d, nSyncHeight=%d, peer=%d\n",
                  pCurrentBlockIndex->nHeight, stats.nSyncHeight, pnode->id);
        return false;
    }

    return true;
}

bool CLibernodeSync::IsBlockchainSynced(bool fBlockAccepted) {
    static bool fBlockchainSynced = false;
    static int64_t nTimeLastProcess = GetTime();
    static int nSkipped = 0;
    static bool fFirstBlockAccepted = false;

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (GetTime() - nTimeLastProcess > 60 * 60) {
        LogPrintf("CLibernodeSync::IsBlockchainSynced time-check fBlockchainSynced=%s\n", fBlockchainSynced);
        Reset();
        fBlockchainSynced = false;
    }

    if (!pCurrentBlockIndex || !pindexBestHeader || fImporting || fReindex) return false;

    if (fBlockAccepted) {
        // this should be only triggered while we are still syncing
        if (!IsSynced()) {
            // we are trying to download smth, reset blockchain sync status
            fFirstBlockAccepted = true;
            fBlockchainSynced = false;
            nTimeLastProcess = GetTime();
            return false;
        }
    } else {
        // skip if we already checked less than 1 tick ago
        if (GetTime() - nTimeLastProcess < LIBERNODE_SYNC_TICK_SECONDS) {
            nSkipped++;
            return fBlockchainSynced;
        }
    }

    LogPrint("libernode-sync", "CLibernodeSync::IsBlockchainSynced -- state before check: %ssynced, skipped %d times\n", fBlockchainSynced ? "" : "not ", nSkipped);

    nTimeLastProcess = GetTime();
    nSkipped = 0;

    if (fBlockchainSynced){
        return true;
    }

    if (fCheckpointsEnabled && pCurrentBlockIndex->nHeight < Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints())) {
        return false;
    }

    std::vector < CNode * > vNodesCopy = CopyNodeVector();
    // We have enough peers and assume most of them are synced
    if (vNodesCopy.size() >= LIBERNODE_SYNC_ENOUGH_PEERS) {
        // Check to see how many of our peers are (almost) at the same height as we are
        int nNodesAtSameHeight = 0;
        BOOST_FOREACH(CNode * pnode, vNodesCopy)
        {
            // Make sure this peer is presumably at the same height
            if (!CheckNodeHeight(pnode)) {
                continue;
            }
            nNodesAtSameHeight++;
            // if we have decent number of such peers, most likely we are synced now
            if (nNodesAtSameHeight >= LIBERNODE_SYNC_ENOUGH_PEERS) {
                LogPrintf("CLibernodeSync::IsBlockchainSynced -- found enough peers on the same height as we are, done\n");
                fBlockchainSynced = true;
                ReleaseNodeVector(vNodesCopy);
                return true;
            }
        }
    }
    ReleaseNodeVector(vNodesCopy);

    // wait for at least one new block to be accepted
    if (!fFirstBlockAccepted) return false;

    // same as !IsInitialBlockDownload() but no cs_main needed here
    int64_t nMaxBlockTime = std::max(pCurrentBlockIndex->GetBlockTime(), pindexBestHeader->GetBlockTime());
    fBlockchainSynced = pindexBestHeader->nHeight - pCurrentBlockIndex->nHeight < 24 * 6 &&
                        GetTime() - nMaxBlockTime < Params().MaxTipAge();
    return fBlockchainSynced;
}

void CLibernodeSync::Fail() {
    nTimeLastFailure = GetTime();
    nRequestedLibernodeAssets = LIBERNODE_SYNC_FAILED;
}

void CLibernodeSync::Reset() {
    nRequestedLibernodeAssets = LIBERNODE_SYNC_INITIAL;
    nRequestedLibernodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
    nTimeLastLibernodeList = GetTime();
    nTimeLastPaymentVote = GetTime();
    nTimeLastGovernanceItem = GetTime();
    nTimeLastFailure = 0;
    nCountFailures = 0;
}

std::string CLibernodeSync::GetAssetName() {
    switch (nRequestedLibernodeAssets) {
        case (LIBERNODE_SYNC_INITIAL):
            return "LIBERNODE_SYNC_INITIAL";
        case (LIBERNODE_SYNC_SPORKS):
            return "LIBERNODE_SYNC_SPORKS";
        case (LIBERNODE_SYNC_LIST):
            return "LIBERNODE_SYNC_LIST";
        case (LIBERNODE_SYNC_MNW):
            return "LIBERNODE_SYNC_MNW";
        case (LIBERNODE_SYNC_FAILED):
            return "LIBERNODE_SYNC_FAILED";
        case LIBERNODE_SYNC_FINISHED:
            return "LIBERNODE_SYNC_FINISHED";
        default:
            return "UNKNOWN";
    }
}

void CLibernodeSync::SwitchToNextAsset() {
    switch (nRequestedLibernodeAssets) {
        case (LIBERNODE_SYNC_FAILED):
            throw std::runtime_error("Can't switch to next asset from failed, should use Reset() first!");
            break;
        case (LIBERNODE_SYNC_INITIAL):
            ClearFulfilledRequests();
            nRequestedLibernodeAssets = LIBERNODE_SYNC_SPORKS;
            LogPrintf("CLibernodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case (LIBERNODE_SYNC_SPORKS):
            nTimeLastLibernodeList = GetTime();
            nRequestedLibernodeAssets = LIBERNODE_SYNC_LIST;
            LogPrintf("CLibernodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;
        case (LIBERNODE_SYNC_LIST):
            nTimeLastPaymentVote = GetTime();
            nRequestedLibernodeAssets = LIBERNODE_SYNC_MNW;
            LogPrintf("CLibernodeSync::SwitchToNextAsset -- Starting %s\n", GetAssetName());
            break;

        case (LIBERNODE_SYNC_MNW):
            nTimeLastGovernanceItem = GetTime();
            LogPrintf("CLibernodeSync::SwitchToNextAsset -- Sync has finished\n");
            nRequestedLibernodeAssets = LIBERNODE_SYNC_FINISHED;
            break;
    }
    nRequestedLibernodeAttempt = 0;
    nTimeAssetSyncStarted = GetTime();
}

std::string CLibernodeSync::GetSyncStatus() {
    switch (libernodeSync.nRequestedLibernodeAssets) {
        case LIBERNODE_SYNC_INITIAL:
            return _("Synchronization pending...");
        case LIBERNODE_SYNC_SPORKS:
            return _("Synchronizing sporks...");
        case LIBERNODE_SYNC_LIST:
            return _("Synchronizing libernodes...");
        case LIBERNODE_SYNC_MNW:
            return _("Synchronizing libernode payments...");
        case LIBERNODE_SYNC_FAILED:
            return _("Synchronization failed");
        case LIBERNODE_SYNC_FINISHED:
            return _("Synchronization finished");
        default:
            return "";
    }
}

void CLibernodeSync::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {
    if (strCommand == NetMsgType::SYNCSTATUSCOUNT) { //Sync status count

        //do not care about stats if sync process finished or failed
        if (IsSynced() || IsFailed()) return;

        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        LogPrintf("SYNCSTATUSCOUNT -- got inventory count: nItemID=%d  nCount=%d  peer=%d\n", nItemID, nCount, pfrom->id);
    }
}

void CLibernodeSync::ClearFulfilledRequests() {
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH(CNode * pnode, vNodes)
    {
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "spork-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "libernode-list-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "libernode-payment-sync");
        netfulfilledman.RemoveFulfilledRequest(pnode->addr, "full-sync");
    }
}

void CLibernodeSync::ProcessTick() {
    static int nTick = 0;
    if (nTick++ % LIBERNODE_SYNC_TICK_SECONDS != 0) return;
    if (!pCurrentBlockIndex) return;

    //the actual count of libernodes we have currently
    int nMnCount = mnodeman.CountLibernodes();

    LogPrint("ProcessTick", "CLibernodeSync::ProcessTick -- nTick %d nMnCount %d\n", nTick, nMnCount);

    // INITIAL SYNC SETUP / LOG REPORTING
    double nSyncProgress = double(nRequestedLibernodeAttempt + (nRequestedLibernodeAssets - 1) * 8) / (8 * 4);
    LogPrint("ProcessTick", "CLibernodeSync::ProcessTick -- nTick %d nRequestedLibernodeAssets %d nRequestedLibernodeAttempt %d nSyncProgress %f\n", nTick, nRequestedLibernodeAssets, nRequestedLibernodeAttempt, nSyncProgress);
    uiInterface.NotifyAdditionalDataSyncProgressChanged(pCurrentBlockIndex->nHeight, nSyncProgress);

    // RESET SYNCING INCASE OF FAILURE
    {
        if (IsSynced()) {
            /*
                Resync if we lost all libernodes from sleep/wake or failed to sync originally
            */
            if (nMnCount == 0) {
                LogPrintf("CLibernodeSync::ProcessTick -- WARNING: not enough data, restarting sync\n");
                Reset();
            } else {
                std::vector < CNode * > vNodesCopy = CopyNodeVector();
                ReleaseNodeVector(vNodesCopy);
                return;
            }
        }

        //try syncing again
        if (IsFailed()) {
            if (nTimeLastFailure + (1 * 60) < GetTime()) { // 1 minute cooldown after failed sync
                Reset();
            }
            return;
        }
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !IsBlockchainSynced() && nRequestedLibernodeAssets > LIBERNODE_SYNC_SPORKS) {
        nTimeLastLibernodeList = GetTime();
        nTimeLastPaymentVote = GetTime();
        nTimeLastGovernanceItem = GetTime();
        return;
    }
    if (nRequestedLibernodeAssets == LIBERNODE_SYNC_INITIAL || (nRequestedLibernodeAssets == LIBERNODE_SYNC_SPORKS && IsBlockchainSynced())) {
        SwitchToNextAsset();
    }

    std::vector < CNode * > vNodesCopy = CopyNodeVector();

    BOOST_FOREACH(CNode * pnode, vNodesCopy)
    {
        // Don't try to sync any data from outbound "libernode" connections -
        // they are temporary and should be considered unreliable for a sync process.
        // Inbound connection this early is most likely a "libernode" connection
        // initialted from another node, so skip it too.
        if (pnode->fLibernode || (fLiberNode && pnode->fInbound)) continue;

        // QUICK MODE (REGTEST ONLY!)
        if (Params().NetworkIDString() == CBaseChainParams::REGTEST) {
            if (nRequestedLibernodeAttempt <= 2) {
                pnode->PushMessage(NetMsgType::GETSPORKS); //get current network sporks
            } else if (nRequestedLibernodeAttempt < 4) {
                mnodeman.DsegUpdate(pnode);
            } else if (nRequestedLibernodeAttempt < 6) {
                int nMnCount = mnodeman.CountLibernodes();
                pnode->PushMessage(NetMsgType::LIBERNODEPAYMENTSYNC, nMnCount); //sync payment votes
            } else {
                nRequestedLibernodeAssets = LIBERNODE_SYNC_FINISHED;
            }
            nRequestedLibernodeAttempt++;
            ReleaseNodeVector(vNodesCopy);
            return;
        }

        // NORMAL NETWORK MODE - TESTNET/MAINNET
        {
            if (netfulfilledman.HasFulfilledRequest(pnode->addr, "full-sync")) {
                // We already fully synced from this node recently,
                // disconnect to free this connection slot for another peer.
                pnode->fDisconnect = true;
                LogPrintf("CLibernodeSync::ProcessTick -- disconnecting from recently synced peer %d\n", pnode->id);
                continue;
            }

            // SPORK : ALWAYS ASK FOR SPORKS AS WE SYNC (we skip this mode now)

            if (!netfulfilledman.HasFulfilledRequest(pnode->addr, "spork-sync")) {
                // only request once from each peer
                netfulfilledman.AddFulfilledRequest(pnode->addr, "spork-sync");
                // get current network sporks
                pnode->PushMessage(NetMsgType::GETSPORKS);
                LogPrintf("CLibernodeSync::ProcessTick -- nTick %d nRequestedLibernodeAssets %d -- requesting sporks from peer %d\n", nTick, nRequestedLibernodeAssets, pnode->id);
                continue; // always get sporks first, switch to the next node without waiting for the next tick
            }

            // MNLIST : SYNC LIBERNODE LIST FROM OTHER CONNECTED CLIENTS

            if (nRequestedLibernodeAssets == LIBERNODE_SYNC_LIST) {
                // check for timeout first
                if (nTimeLastLibernodeList < GetTime() - LIBERNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CLibernodeSync::ProcessTick -- nTick %d nRequestedLibernodeAssets %d -- timeout\n", nTick, nRequestedLibernodeAssets);
                    if (nRequestedLibernodeAttempt == 0) {
                        LogPrintf("CLibernodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // there is no way we can continue without libernode list, fail here and try later
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "libernode-list-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "libernode-list-sync");

                if (pnode->nVersion < mnpayments.GetMinLibernodePaymentsProto()) continue;
                nRequestedLibernodeAttempt++;

                mnodeman.DsegUpdate(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

            // MNW : SYNC LIBERNODE PAYMENT VOTES FROM OTHER CONNECTED CLIENTS

            if (nRequestedLibernodeAssets == LIBERNODE_SYNC_MNW) {
                LogPrint("mnpayments", "CLibernodeSync::ProcessTick -- nTick %d nRequestedLibernodeAssets %d nTimeLastPaymentVote %lld GetTime() %lld diff %lld\n", nTick, nRequestedLibernodeAssets, nTimeLastPaymentVote, GetTime(), GetTime() - nTimeLastPaymentVote);
                // check for timeout first
                // This might take a lot longer than LIBERNODE_SYNC_TIMEOUT_SECONDS minutes due to new blocks,
                // but that should be OK and it should timeout eventually.
                if (nTimeLastPaymentVote < GetTime() - LIBERNODE_SYNC_TIMEOUT_SECONDS) {
                    LogPrintf("CLibernodeSync::ProcessTick -- nTick %d nRequestedLibernodeAssets %d -- timeout\n", nTick, nRequestedLibernodeAssets);
                    if (nRequestedLibernodeAttempt == 0) {
                        LogPrintf("CLibernodeSync::ProcessTick -- ERROR: failed to sync %s\n", GetAssetName());
                        // probably not a good idea to proceed without winner list
                        Fail();
                        ReleaseNodeVector(vNodesCopy);
                        return;
                    }
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // check for data
                // if mnpayments already has enough blocks and votes, switch to the next asset
                // try to fetch data from at least two peers though
                if (nRequestedLibernodeAttempt > 1 && mnpayments.IsEnoughData()) {
                    LogPrintf("CLibernodeSync::ProcessTick -- nTick %d nRequestedLibernodeAssets %d -- found enough data\n", nTick, nRequestedLibernodeAssets);
                    SwitchToNextAsset();
                    ReleaseNodeVector(vNodesCopy);
                    return;
                }

                // only request once from each peer
                if (netfulfilledman.HasFulfilledRequest(pnode->addr, "libernode-payment-sync")) continue;
                netfulfilledman.AddFulfilledRequest(pnode->addr, "libernode-payment-sync");

                if (pnode->nVersion < mnpayments.GetMinLibernodePaymentsProto()) continue;
                nRequestedLibernodeAttempt++;

                // ask node for all payment votes it has (new nodes will only return votes for future payments)
                pnode->PushMessage(NetMsgType::LIBERNODEPAYMENTSYNC, mnpayments.GetStorageLimit());
                // ask node for missing pieces only (old nodes will not be asked)
                mnpayments.RequestLowDataPaymentBlocks(pnode);

                ReleaseNodeVector(vNodesCopy);
                return; //this will cause each peer to get one request each six seconds for the various assets we need
            }

        }
    }
    // looped through all nodes, release them
    ReleaseNodeVector(vNodesCopy);
}

void CLibernodeSync::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
}
