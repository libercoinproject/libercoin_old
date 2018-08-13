// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activelibernode.h"
#include "libernode.h"
#include "libernode-sync.h"
#include "libernodeman.h"
#include "protocol.h"

extern CWallet *pwalletMain;

// Keep track of the active Libernode
CActiveLibernode activeLibernode;

void CActiveLibernode::ManageState() {
    LogPrint("libernode", "CActiveLibernode::ManageState -- Start\n");
    if (!fLiberNode) {
        LogPrint("libernode", "CActiveLibernode::ManageState -- Not a libernode, returning\n");
        return;
    }

    if (Params().NetworkIDString() != CBaseChainParams::REGTEST && !libernodeSync.IsBlockchainSynced()) {
        nState = ACTIVE_LIBERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveLibernode::ManageState -- %s: %s\n", GetStateString(), GetStatus());
        return;
    }

    if (nState == ACTIVE_LIBERNODE_SYNC_IN_PROCESS) {
        nState = ACTIVE_LIBERNODE_INITIAL;
    }

    LogPrint("libernode", "CActiveLibernode::ManageState -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    if (eType == LIBERNODE_UNKNOWN) {
        ManageStateInitial();
    }

    if (eType == LIBERNODE_REMOTE) {
        ManageStateRemote();
    } else if (eType == LIBERNODE_LOCAL) {
        // Try Remote Start first so the started local libernode can be restarted without recreate libernode broadcast.
        ManageStateRemote();
        if (nState != ACTIVE_LIBERNODE_STARTED)
            ManageStateLocal();
    }

    SendLibernodePing();
}

std::string CActiveLibernode::GetStateString() const {
    switch (nState) {
        case ACTIVE_LIBERNODE_INITIAL:
            return "INITIAL";
        case ACTIVE_LIBERNODE_SYNC_IN_PROCESS:
            return "SYNC_IN_PROCESS";
        case ACTIVE_LIBERNODE_INPUT_TOO_NEW:
            return "INPUT_TOO_NEW";
        case ACTIVE_LIBERNODE_NOT_CAPABLE:
            return "NOT_CAPABLE";
        case ACTIVE_LIBERNODE_STARTED:
            return "STARTED";
        default:
            return "UNKNOWN";
    }
}

std::string CActiveLibernode::GetStatus() const {
    switch (nState) {
        case ACTIVE_LIBERNODE_INITIAL:
            return "Node just started, not yet activated";
        case ACTIVE_LIBERNODE_SYNC_IN_PROCESS:
            return "Sync in progress. Must wait until sync is complete to start Libernode";
        case ACTIVE_LIBERNODE_INPUT_TOO_NEW:
            return strprintf("Libernode input must have at least %d confirmations",
                             Params().GetConsensus().nLibernodeMinimumConfirmations);
        case ACTIVE_LIBERNODE_NOT_CAPABLE:
            return "Not capable libernode: " + strNotCapableReason;
        case ACTIVE_LIBERNODE_STARTED:
            return "Libernode successfully started";
        default:
            return "Unknown";
    }
}

std::string CActiveLibernode::GetTypeString() const {
    std::string strType;
    switch (eType) {
        case LIBERNODE_UNKNOWN:
            strType = "UNKNOWN";
            break;
        case LIBERNODE_REMOTE:
            strType = "REMOTE";
            break;
        case LIBERNODE_LOCAL:
            strType = "LOCAL";
            break;
        default:
            strType = "UNKNOWN";
            break;
    }
    return strType;
}

bool CActiveLibernode::SendLibernodePing() {
    if (!fPingerEnabled) {
        LogPrint("libernode",
                 "CActiveLibernode::SendLibernodePing -- %s: libernode ping service is disabled, skipping...\n",
                 GetStateString());
        return false;
    }

    if (!mnodeman.Has(vin)) {
        strNotCapableReason = "Libernode not in libernode list";
        nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
        LogPrintf("CActiveLibernode::SendLibernodePing -- %s: %s\n", GetStateString(), strNotCapableReason);
        return false;
    }

    CLibernodePing mnp(vin);
    if (!mnp.Sign(keyLibernode, pubKeyLibernode)) {
        LogPrintf("CActiveLibernode::SendLibernodePing -- ERROR: Couldn't sign Libernode Ping\n");
        return false;
    }

    // Update lastPing for our libernode in Libernode list
    if (mnodeman.IsLibernodePingedWithin(vin, LIBERNODE_MIN_MNP_SECONDS, mnp.sigTime)) {
        LogPrintf("CActiveLibernode::SendLibernodePing -- Too early to send Libernode Ping\n");
        return false;
    }

    mnodeman.SetLibernodeLastPing(vin, mnp);

    LogPrintf("CActiveLibernode::SendLibernodePing -- Relaying ping, collateral=%s\n", vin.ToString());
    mnp.Relay();

    return true;
}

void CActiveLibernode::ManageStateInitial() {
    LogPrint("libernode", "CActiveLibernode::ManageStateInitial -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);

    // Check that our local network configuration is correct
    if (!fListen) {
        // listen option is probably overwritten by smth else, no good
        nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
        strNotCapableReason = "Libernode must accept connections from outside. Make sure listen configuration option is not overwritten by some another parameter.";
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    bool fFoundLocal = false;
    {
        LOCK(cs_vNodes);

        // First try to find whatever local address is specified by externalip option
        fFoundLocal = GetLocal(service) && CLibernode::IsValidNetAddr(service);
        if (!fFoundLocal) {
            // nothing and no live connections, can't do anything for now
            if (vNodes.empty()) {
                nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
                strNotCapableReason = "Can't detect valid external address. Will retry when there are some connections available.";
                LogPrintf("CActiveLibernode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
                return;
            }
            // We have some peers, let's try to find our local address from one of them
            BOOST_FOREACH(CNode * pnode, vNodes)
            {
                if (pnode->fSuccessfullyConnected && pnode->addr.IsIPv4()) {
                    fFoundLocal = GetLocal(service, &pnode->addr) && CLibernode::IsValidNetAddr(service);
                    if (fFoundLocal) break;
                }
            }
        }
    }

    if (!fFoundLocal) {
        nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
        strNotCapableReason = "Can't detect valid external address. Please consider using the externalip configuration option if problem persists. Make sure to use IPv4 address only.";
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Invalid port: %u - only %d is supported on mainnet.", service.GetPort(),
                                            mainnetDefaultPort);
            LogPrintf("CActiveLibernode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
        strNotCapableReason = strprintf("Invalid port: %u - %d is only supported on mainnet.", service.GetPort(),
                                        mainnetDefaultPort);
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    LogPrintf("CActiveLibernode::ManageStateInitial -- Checking inbound connection to '%s'\n", service.ToString());
    //TODO
    if (!ConnectNode(CAddress(service, NODE_NETWORK), NULL, false, true)) {
        nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
        strNotCapableReason = "Could not connect to " + service.ToString();
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: %s\n", GetStateString(), strNotCapableReason);
        return;
    }

    // Default to REMOTE
    eType = LIBERNODE_REMOTE;

    // Check if wallet funds are available
    if (!pwalletMain) {
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: Wallet not available\n", GetStateString());
        return;
    }

    if (pwalletMain->IsLocked()) {
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: Wallet is locked\n", GetStateString());
        return;
    }

    if (pwalletMain->GetBalance() < LIBERNODE_COIN_REQUIRED * COIN) {
        LogPrintf("CActiveLibernode::ManageStateInitial -- %s: Wallet balance is < 1000 XZC\n", GetStateString());
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    // If collateral is found switch to LOCAL mode

    if (pwalletMain->GetLibernodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        eType = LIBERNODE_LOCAL;
    }

    LogPrint("libernode", "CActiveLibernode::ManageStateInitial -- End status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
}

void CActiveLibernode::ManageStateRemote() {
    LogPrint("libernode",
             "CActiveLibernode::ManageStateRemote -- Start status = %s, type = %s, pinger enabled = %d, pubKeyLibernode.GetID() = %s\n",
             GetStatus(), fPingerEnabled, GetTypeString(), pubKeyLibernode.GetID().ToString());

    mnodeman.CheckLibernode(pubKeyLibernode);
    libernode_info_t infoMn = mnodeman.GetLibernodeInfo(pubKeyLibernode);
    if (infoMn.fInfoValid) {
        if (infoMn.nProtocolVersion != PROTOCOL_VERSION) {
            nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
            strNotCapableReason = "Invalid protocol version";
            LogPrintf("CActiveLibernode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (service != infoMn.addr) {
            nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
            // LogPrintf("service: %s\n", service.ToString());
            // LogPrintf("infoMn.addr: %s\n", infoMn.addr.ToString());
            strNotCapableReason = "Broadcasted IP doesn't match our external address. Make sure you issued a new broadcast if IP of this libernode changed recently.";
            LogPrintf("CActiveLibernode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (!CLibernode::IsValidStateForAutoStart(infoMn.nActiveState)) {
            nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
            strNotCapableReason = strprintf("Libernode in %s state", CLibernode::StateToString(infoMn.nActiveState));
            LogPrintf("CActiveLibernode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }
        if (nState != ACTIVE_LIBERNODE_STARTED) {
            LogPrintf("CActiveLibernode::ManageStateRemote -- STARTED!\n");
            vin = infoMn.vin;
            service = infoMn.addr;
            fPingerEnabled = true;
            nState = ACTIVE_LIBERNODE_STARTED;
        }
    } else {
        nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
        strNotCapableReason = "Libernode not in libernode list";
        LogPrintf("CActiveLibernode::ManageStateRemote -- %s: %s\n", GetStateString(), strNotCapableReason);
    }
}

void CActiveLibernode::ManageStateLocal() {
    LogPrint("libernode", "CActiveLibernode::ManageStateLocal -- status = %s, type = %s, pinger enabled = %d\n",
             GetStatus(), GetTypeString(), fPingerEnabled);
    if (nState == ACTIVE_LIBERNODE_STARTED) {
        return;
    }

    // Choose coins to use
    CPubKey pubKeyCollateral;
    CKey keyCollateral;

    if (pwalletMain->GetLibernodeVinAndKeys(vin, pubKeyCollateral, keyCollateral)) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge < Params().GetConsensus().nLibernodeMinimumConfirmations) {
            nState = ACTIVE_LIBERNODE_INPUT_TOO_NEW;
            strNotCapableReason = strprintf(_("%s - %d confirmations"), GetStatus(), nInputAge);
            LogPrintf("CActiveLibernode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->LockCoin(vin.prevout);
        }

        CLibernodeBroadcast mnb;
        std::string strError;
        if (!CLibernodeBroadcast::Create(vin, service, keyCollateral, pubKeyCollateral, keyLibernode,
                                     pubKeyLibernode, strError, mnb)) {
            nState = ACTIVE_LIBERNODE_NOT_CAPABLE;
            strNotCapableReason = "Error creating mastenode broadcast: " + strError;
            LogPrintf("CActiveLibernode::ManageStateLocal -- %s: %s\n", GetStateString(), strNotCapableReason);
            return;
        }

        fPingerEnabled = true;
        nState = ACTIVE_LIBERNODE_STARTED;

        //update to libernode list
        LogPrintf("CActiveLibernode::ManageStateLocal -- Update Libernode List\n");
        mnodeman.UpdateLibernodeList(mnb);
        mnodeman.NotifyLibernodeUpdates();

        //send to all peers
        LogPrintf("CActiveLibernode::ManageStateLocal -- Relay broadcast, vin=%s\n", vin.ToString());
        mnb.RelayLiberNode();
    }
}
