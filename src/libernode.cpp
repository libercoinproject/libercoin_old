// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activelibernode.h"
#include "consensus/validation.h"
#include "darksend.h"
#include "init.h"
//#include "governance.h"
#include "libernode.h"
#include "libernode-payments.h"
#include "libernode-sync.h"
#include "libernodeman.h"
#include "util.h"

#include <boost/lexical_cast.hpp>


CLibernode::CLibernode() :
        vin(),
        addr(),
        pubKeyCollateralAddress(),
        pubKeyLibernode(),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(LIBERNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(PROTOCOL_VERSION),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CLibernode::CLibernode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyLibernodeNew, int nProtocolVersionIn) :
        vin(vinNew),
        addr(addrNew),
        pubKeyCollateralAddress(pubKeyCollateralAddressNew),
        pubKeyLibernode(pubKeyLibernodeNew),
        lastPing(),
        vchSig(),
        sigTime(GetAdjustedTime()),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(0),
        nActiveState(LIBERNODE_ENABLED),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(nProtocolVersionIn),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

CLibernode::CLibernode(const CLibernode &other) :
        vin(other.vin),
        addr(other.addr),
        pubKeyCollateralAddress(other.pubKeyCollateralAddress),
        pubKeyLibernode(other.pubKeyLibernode),
        lastPing(other.lastPing),
        vchSig(other.vchSig),
        sigTime(other.sigTime),
        nLastDsq(other.nLastDsq),
        nTimeLastChecked(other.nTimeLastChecked),
        nTimeLastPaid(other.nTimeLastPaid),
        nTimeLastWatchdogVote(other.nTimeLastWatchdogVote),
        nActiveState(other.nActiveState),
        nCacheCollateralBlock(other.nCacheCollateralBlock),
        nBlockLastPaid(other.nBlockLastPaid),
        nProtocolVersion(other.nProtocolVersion),
        nPoSeBanScore(other.nPoSeBanScore),
        nPoSeBanHeight(other.nPoSeBanHeight),
        fAllowMixingTx(other.fAllowMixingTx),
        fUnitTest(other.fUnitTest) {}

CLibernode::CLibernode(const CLibernodeBroadcast &mnb) :
        vin(mnb.vin),
        addr(mnb.addr),
        pubKeyCollateralAddress(mnb.pubKeyCollateralAddress),
        pubKeyLibernode(mnb.pubKeyLibernode),
        lastPing(mnb.lastPing),
        vchSig(mnb.vchSig),
        sigTime(mnb.sigTime),
        nLastDsq(0),
        nTimeLastChecked(0),
        nTimeLastPaid(0),
        nTimeLastWatchdogVote(mnb.sigTime),
        nActiveState(mnb.nActiveState),
        nCacheCollateralBlock(0),
        nBlockLastPaid(0),
        nProtocolVersion(mnb.nProtocolVersion),
        nPoSeBanScore(0),
        nPoSeBanHeight(0),
        fAllowMixingTx(true),
        fUnitTest(false) {}

//CSporkManager sporkManager;
//
// When a new libernode broadcast is sent, update our information
//
bool CLibernode::UpdateFromNewBroadcast(CLibernodeBroadcast &mnb) {
    if (mnb.sigTime <= sigTime && !mnb.fRecovery) return false;

    pubKeyLibernode = mnb.pubKeyLibernode;
    sigTime = mnb.sigTime;
    vchSig = mnb.vchSig;
    nProtocolVersion = mnb.nProtocolVersion;
    addr = mnb.addr;
    nPoSeBanScore = 0;
    nPoSeBanHeight = 0;
    nTimeLastChecked = 0;
    int nDos = 0;
    if (mnb.lastPing == CLibernodePing() || (mnb.lastPing != CLibernodePing() && mnb.lastPing.CheckAndUpdate(this, true, nDos))) {
        lastPing = mnb.lastPing;
        mnodeman.mapSeenLibernodePing.insert(std::make_pair(lastPing.GetHash(), lastPing));
    }
    // if it matches our Libernode privkey...
    if (fLiberNode && pubKeyLibernode == activeLibernode.pubKeyLibernode) {
        nPoSeBanScore = -LIBERNODE_POSE_BAN_MAX_SCORE;
        if (nProtocolVersion == PROTOCOL_VERSION) {
            // ... and PROTOCOL_VERSION, then we've been remotely activated ...
            activeLibernode.ManageState();
        } else {
            // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
            // but also do not ban the node we get this message from
            LogPrintf("CLibernode::UpdateFromNewBroadcast -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", nProtocolVersion, PROTOCOL_VERSION);
            return false;
        }
    }
    return true;
}

//
// Deterministically calculate a given "score" for a Libernode depending on how close it's hash is to
// the proof of work for that block. The further away they are the better, the furthest will win the election
// and get paid this block
//
arith_uint256 CLibernode::CalculateScore(const uint256 &blockHash) {
    uint256 aux = ArithToUint256(UintToArith256(vin.prevout.hash) + vin.prevout.n);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << blockHash;
    arith_uint256 hash2 = UintToArith256(ss.GetHash());

    CHashWriter ss2(SER_GETHASH, PROTOCOL_VERSION);
    ss2 << blockHash;
    ss2 << aux;
    arith_uint256 hash3 = UintToArith256(ss2.GetHash());

    return (hash3 > hash2 ? hash3 - hash2 : hash2 - hash3);
}

void CLibernode::Check(bool fForce) {
    LOCK(cs);

    if (ShutdownRequested()) return;

    if (!fForce && (GetTime() - nTimeLastChecked < LIBERNODE_CHECK_SECONDS)) return;
    nTimeLastChecked = GetTime();

    LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state\n", vin.prevout.ToStringShort(), GetStateString());

    //once spent, stop doing the checks
    if (IsOutpointSpent()) return;

    int nHeight = 0;
    if (!fUnitTest) {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) return;

        CCoins coins;
        if (!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
            (unsigned int) vin.prevout.n >= coins.vout.size() ||
            coins.vout[vin.prevout.n].IsNull()) {
            nActiveState = LIBERNODE_OUTPOINT_SPENT;
            LogPrint("libernode", "CLibernode::Check -- Failed to find Libernode UTXO, libernode=%s\n", vin.prevout.ToStringShort());
            return;
        }

        nHeight = chainActive.Height();
    }

    if (IsPoSeBanned()) {
        if (nHeight < nPoSeBanHeight) return; // too early?
        // Otherwise give it a chance to proceed further to do all the usual checks and to change its state.
        // Libernode still will be on the edge and can be banned back easily if it keeps ignoring mnverify
        // or connect attempts. Will require few mnverify messages to strengthen its position in mn list.
        LogPrintf("CLibernode::Check -- Libernode %s is unbanned and back in list now\n", vin.prevout.ToStringShort());
        DecreasePoSeBanScore();
    } else if (nPoSeBanScore >= LIBERNODE_POSE_BAN_MAX_SCORE) {
        nActiveState = LIBERNODE_POSE_BAN;
        // ban for the whole payment cycle
        nPoSeBanHeight = nHeight + mnodeman.size();
        LogPrintf("CLibernode::Check -- Libernode %s is banned till block %d now\n", vin.prevout.ToStringShort(), nPoSeBanHeight);
        return;
    }

    int nActiveStatePrev = nActiveState;
    bool fOurLibernode = fLiberNode && activeLibernode.pubKeyLibernode == pubKeyLibernode;

    // libernode doesn't meet payment protocol requirements ...
    bool fRequireUpdate = nProtocolVersion < mnpayments.GetMinLibernodePaymentsProto() ||
                          // or it's our own node and we just updated it to the new protocol but we are still waiting for activation ...
                          (fOurLibernode && nProtocolVersion < PROTOCOL_VERSION);

    if (fRequireUpdate) {
        nActiveState = LIBERNODE_UPDATE_REQUIRED;
        if (nActiveStatePrev != nActiveState) {
            LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    // keep old libernodes on start, give them a chance to receive updates...
    bool fWaitForPing = !libernodeSync.IsLibernodeListSynced() && !IsPingedWithin(LIBERNODE_MIN_MNP_SECONDS);

    if (fWaitForPing && !fOurLibernode) {
        // ...but if it was already expired before the initial check - return right away
        if (IsExpired() || IsWatchdogExpired() || IsNewStartRequired()) {
            LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state, waiting for ping\n", vin.prevout.ToStringShort(), GetStateString());
            return;
        }
    }

    // don't expire if we are still in "waiting for ping" mode unless it's our own libernode
    if (!fWaitForPing || fOurLibernode) {

        if (!IsPingedWithin(LIBERNODE_NEW_START_REQUIRED_SECONDS)) {
            nActiveState = LIBERNODE_NEW_START_REQUIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        bool fWatchdogActive = libernodeSync.IsSynced() && mnodeman.IsWatchdogActive();
        bool fWatchdogExpired = (fWatchdogActive && ((GetTime() - nTimeLastWatchdogVote) > LIBERNODE_WATCHDOG_MAX_SECONDS));

//        LogPrint("libernode", "CLibernode::Check -- outpoint=%s, nTimeLastWatchdogVote=%d, GetTime()=%d, fWatchdogExpired=%d\n",
//                vin.prevout.ToStringShort(), nTimeLastWatchdogVote, GetTime(), fWatchdogExpired);

        if (fWatchdogExpired) {
            nActiveState = LIBERNODE_WATCHDOG_EXPIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }

        if (!IsPingedWithin(LIBERNODE_EXPIRATION_SECONDS)) {
            nActiveState = LIBERNODE_EXPIRED;
            if (nActiveStatePrev != nActiveState) {
                LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
            }
            return;
        }
    }

    if (lastPing.sigTime - sigTime < LIBERNODE_MIN_MNP_SECONDS) {
        nActiveState = LIBERNODE_PRE_ENABLED;
        if (nActiveStatePrev != nActiveState) {
            LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
        }
        return;
    }

    nActiveState = LIBERNODE_ENABLED; // OK
    if (nActiveStatePrev != nActiveState) {
        LogPrint("libernode", "CLibernode::Check -- Libernode %s is in %s state now\n", vin.prevout.ToStringShort(), GetStateString());
    }
}

bool CLibernode::IsValidNetAddr() {
    return IsValidNetAddr(addr);
}

bool CLibernode::IsValidForPayment() {
    if (nActiveState == LIBERNODE_ENABLED) {
        return true;
    }
//    if(!sporkManager.IsSporkActive(SPORK_14_REQUIRE_SENTINEL_FLAG) &&
//       (nActiveState == LIBERNODE_WATCHDOG_EXPIRED)) {
//        return true;
//    }

    return false;
}

bool CLibernode::IsValidNetAddr(CService addrIn) {
    // TODO: regtest is fine with any addresses for now,
    // should probably be a bit smarter if one day we start to implement tests for this
    return Params().NetworkIDString() == CBaseChainParams::REGTEST ||
           (addrIn.IsIPv4() && IsReachable(addrIn) && addrIn.IsRoutable());
}

libernode_info_t CLibernode::GetInfo() {
    libernode_info_t info;
    info.vin = vin;
    info.addr = addr;
    info.pubKeyCollateralAddress = pubKeyCollateralAddress;
    info.pubKeyLibernode = pubKeyLibernode;
    info.sigTime = sigTime;
    info.nLastDsq = nLastDsq;
    info.nTimeLastChecked = nTimeLastChecked;
    info.nTimeLastPaid = nTimeLastPaid;
    info.nTimeLastWatchdogVote = nTimeLastWatchdogVote;
    info.nTimeLastPing = lastPing.sigTime;
    info.nActiveState = nActiveState;
    info.nProtocolVersion = nProtocolVersion;
    info.fInfoValid = true;
    return info;
}

std::string CLibernode::StateToString(int nStateIn) {
    switch (nStateIn) {
        case LIBERNODE_PRE_ENABLED:
            return "PRE_ENABLED";
        case LIBERNODE_ENABLED:
            return "ENABLED";
        case LIBERNODE_EXPIRED:
            return "EXPIRED";
        case LIBERNODE_OUTPOINT_SPENT:
            return "OUTPOINT_SPENT";
        case LIBERNODE_UPDATE_REQUIRED:
            return "UPDATE_REQUIRED";
        case LIBERNODE_WATCHDOG_EXPIRED:
            return "WATCHDOG_EXPIRED";
        case LIBERNODE_NEW_START_REQUIRED:
            return "NEW_START_REQUIRED";
        case LIBERNODE_POSE_BAN:
            return "POSE_BAN";
        default:
            return "UNKNOWN";
    }
}

std::string CLibernode::GetStateString() const {
    return StateToString(nActiveState);
}

std::string CLibernode::GetStatus() const {
    // TODO: return smth a bit more human readable here
    return GetStateString();
}

std::string CLibernode::ToString() const {
    std::string str;
    str += "libernode{";
    str += addr.ToString();
    str += " ";
    str += std::to_string(nProtocolVersion);
    str += " ";
    str += vin.prevout.ToStringShort();
    str += " ";
    str += CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString();
    str += " ";
    str += std::to_string(lastPing == CLibernodePing() ? sigTime : lastPing.sigTime);
    str += " ";
    str += std::to_string(lastPing == CLibernodePing() ? 0 : lastPing.sigTime - sigTime);
    str += " ";
    str += std::to_string(nBlockLastPaid);
    str += "}\n";
    return str;
}

int CLibernode::GetCollateralAge() {
    int nHeight;
    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain || !chainActive.Tip()) return -1;
        nHeight = chainActive.Height();
    }

    if (nCacheCollateralBlock == 0) {
        int nInputAge = GetInputAge(vin);
        if (nInputAge > 0) {
            nCacheCollateralBlock = nHeight - nInputAge;
        } else {
            return nInputAge;
        }
    }

    return nHeight - nCacheCollateralBlock;
}

void CLibernode::UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack) {
    if (!pindex) {
        LogPrintf("CLibernode::UpdateLastPaid pindex is NULL\n");
        return;
    }

    const CBlockIndex *BlockReading = pindex;

    CScript mnpayee = GetScriptForDestination(pubKeyCollateralAddress.GetID());
    LogPrint("libernode", "CLibernode::UpdateLastPaidBlock -- searching for block with payment to %s\n", vin.prevout.ToStringShort());

    LOCK(cs_mapLibernodeBlocks);

    for (int i = 0; BlockReading && BlockReading->nHeight > nBlockLastPaid && i < nMaxBlocksToScanBack; i++) {
//        LogPrintf("mnpayments.mapLibernodeBlocks.count(BlockReading->nHeight)=%s\n", mnpayments.mapLibernodeBlocks.count(BlockReading->nHeight));
//        LogPrintf("mnpayments.mapLibernodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)=%s\n", mnpayments.mapLibernodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2));
        if (mnpayments.mapLibernodeBlocks.count(BlockReading->nHeight) &&
            mnpayments.mapLibernodeBlocks[BlockReading->nHeight].HasPayeeWithVotes(mnpayee, 2)) {
            // LogPrintf("i=%s, BlockReading->nHeight=%s\n", i, BlockReading->nHeight);
            CBlock block;
            if (!ReadBlockFromDisk(block, BlockReading, Params().GetConsensus())) // shouldn't really happen
            {
                LogPrintf("ReadBlockFromDisk failed\n");
                continue;
            }

            CAmount nLibernodePayment = GetLibernodePayment(BlockReading->nHeight, block.vtx[0].GetValueOut());

            BOOST_FOREACH(CTxOut txout, block.vtx[0].vout)
            if (mnpayee == txout.scriptPubKey && nLibernodePayment == txout.nValue) {
                nBlockLastPaid = BlockReading->nHeight;
                nTimeLastPaid = BlockReading->nTime;
                LogPrint("libernode", "CLibernode::UpdateLastPaidBlock -- searching for block with payment to %s -- found new %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
                return;
            }
        }

        if (BlockReading->pprev == NULL) {
            assert(BlockReading);
            break;
        }
        BlockReading = BlockReading->pprev;
    }

    // Last payment for this libernode wasn't found in latest mnpayments blocks
    // or it was found in mnpayments blocks but wasn't found in the blockchain.
    // LogPrint("libernode", "CLibernode::UpdateLastPaidBlock -- searching for block with payment to %s -- keeping old %d\n", vin.prevout.ToStringShort(), nBlockLastPaid);
}

bool CLibernodeBroadcast::Create(std::string strService, std::string strKeyLibernode, std::string strTxHash, std::string strOutputIndex, std::string &strErrorRet, CLibernodeBroadcast &mnbRet, bool fOffline) {
    LogPrintf("CLibernodeBroadcast::Create\n");
    CTxIn txin;
    CPubKey pubKeyCollateralAddressNew;
    CKey keyCollateralAddressNew;
    CPubKey pubKeyLibernodeNew;
    CKey keyLibernodeNew;
    //need correct blocks to send ping
    if (!fOffline && !libernodeSync.IsBlockchainSynced()) {
        strErrorRet = "Sync in progress. Must wait until sync is complete to start Libernode";
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    //TODO
    if (!darkSendSigner.GetKeysFromSecret(strKeyLibernode, keyLibernodeNew, pubKeyLibernodeNew)) {
        strErrorRet = strprintf("Invalid libernode key %s", strKeyLibernode);
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    if (!pwalletMain->GetLibernodeVinAndKeys(txin, pubKeyCollateralAddressNew, keyCollateralAddressNew, strTxHash, strOutputIndex)) {
        strErrorRet = strprintf("Could not allocate txin %s:%s for libernode %s", strTxHash, strOutputIndex, strService);
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    CService service = CService(strService);
    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (service.GetPort() != mainnetDefaultPort) {
            strErrorRet = strprintf("Invalid port %u for libernode %s, only %d is supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
            LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
            return false;
        }
    } else if (service.GetPort() == mainnetDefaultPort) {
        strErrorRet = strprintf("Invalid port %u for libernode %s, %d is the only supported on mainnet.", service.GetPort(), strService, mainnetDefaultPort);
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        return false;
    }

    return Create(txin, CService(strService), keyCollateralAddressNew, pubKeyCollateralAddressNew, keyLibernodeNew, pubKeyLibernodeNew, strErrorRet, mnbRet);
}

bool CLibernodeBroadcast::Create(CTxIn txin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyLibernodeNew, CPubKey pubKeyLibernodeNew, std::string &strErrorRet, CLibernodeBroadcast &mnbRet) {
    // wait for reindex and/or import to finish
    if (fImporting || fReindex) return false;

    LogPrint("libernode", "CLibernodeBroadcast::Create -- pubKeyCollateralAddressNew = %s, pubKeyLibernodeNew.GetID() = %s\n",
             CBitcoinAddress(pubKeyCollateralAddressNew.GetID()).ToString(),
             pubKeyLibernodeNew.GetID().ToString());


    CLibernodePing mnp(txin);
    if (!mnp.Sign(keyLibernodeNew, pubKeyLibernodeNew)) {
        strErrorRet = strprintf("Failed to sign ping, libernode=%s", txin.prevout.ToStringShort());
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CLibernodeBroadcast();
        return false;
    }

    mnbRet = CLibernodeBroadcast(service, txin, pubKeyCollateralAddressNew, pubKeyLibernodeNew, PROTOCOL_VERSION);

    if (!mnbRet.IsValidNetAddr()) {
        strErrorRet = strprintf("Invalid IP address, libernode=%s", txin.prevout.ToStringShort());
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CLibernodeBroadcast();
        return false;
    }

    mnbRet.lastPing = mnp;
    if (!mnbRet.Sign(keyCollateralAddressNew)) {
        strErrorRet = strprintf("Failed to sign broadcast, libernode=%s", txin.prevout.ToStringShort());
        LogPrintf("CLibernodeBroadcast::Create -- %s\n", strErrorRet);
        mnbRet = CLibernodeBroadcast();
        return false;
    }

    return true;
}

bool CLibernodeBroadcast::SimpleCheck(int &nDos) {
    nDos = 0;

    // make sure addr is valid
    if (!IsValidNetAddr()) {
        LogPrintf("CLibernodeBroadcast::SimpleCheck -- Invalid addr, rejected: libernode=%s  addr=%s\n",
                  vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    // make sure signature isn't in the future (past is OK)
    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CLibernodeBroadcast::SimpleCheck -- Signature rejected, too far into the future: libernode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    // empty ping or incorrect sigTime/unknown blockhash
    if (lastPing == CLibernodePing() || !lastPing.SimpleCheck(nDos)) {
        // one of us is probably forked or smth, just mark it as expired and check the rest of the rules
        nActiveState = LIBERNODE_EXPIRED;
    }

    if (nProtocolVersion < mnpayments.GetMinLibernodePaymentsProto()) {
        LogPrintf("CLibernodeBroadcast::SimpleCheck -- ignoring outdated Libernode: libernode=%s  nProtocolVersion=%d\n", vin.prevout.ToStringShort(), nProtocolVersion);
        return false;
    }

    CScript pubkeyScript;
    pubkeyScript = GetScriptForDestination(pubKeyCollateralAddress.GetID());

    if (pubkeyScript.size() != 25) {
        LogPrintf("CLibernodeBroadcast::SimpleCheck -- pubKeyCollateralAddress has the wrong size\n");
        nDos = 100;
        return false;
    }

    CScript pubkeyScript2;
    pubkeyScript2 = GetScriptForDestination(pubKeyLibernode.GetID());

    if (pubkeyScript2.size() != 25) {
        LogPrintf("CLibernodeBroadcast::SimpleCheck -- pubKeyLibernode has the wrong size\n");
        nDos = 100;
        return false;
    }

    if (!vin.scriptSig.empty()) {
        LogPrintf("CLibernodeBroadcast::SimpleCheck -- Ignore Not Empty ScriptSig %s\n", vin.ToString());
        nDos = 100;
        return false;
    }

    int mainnetDefaultPort = Params(CBaseChainParams::MAIN).GetDefaultPort();
    if (Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if (addr.GetPort() != mainnetDefaultPort) return false;
    } else if (addr.GetPort() == mainnetDefaultPort) return false;

    return true;
}

bool CLibernodeBroadcast::Update(CLibernode *pmn, int &nDos) {
    nDos = 0;

    if (pmn->sigTime == sigTime && !fRecovery) {
        // mapSeenLibernodeBroadcast in CLibernodeMan::CheckMnbAndUpdateLibernodeList should filter legit duplicates
        // but this still can happen if we just started, which is ok, just do nothing here.
        return false;
    }

    // this broadcast is older than the one that we already have - it's bad and should never happen
    // unless someone is doing something fishy
    if (pmn->sigTime > sigTime) {
        LogPrintf("CLibernodeBroadcast::Update -- Bad sigTime %d (existing broadcast is at %d) for Libernode %s %s\n",
                  sigTime, pmn->sigTime, vin.prevout.ToStringShort(), addr.ToString());
        return false;
    }

    pmn->Check();

    // libernode is banned by PoSe
    if (pmn->IsPoSeBanned()) {
        LogPrintf("CLibernodeBroadcast::Update -- Banned by PoSe, libernode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // IsVnAssociatedWithPubkey is validated once in CheckOutpoint, after that they just need to match
    if (pmn->pubKeyCollateralAddress != pubKeyCollateralAddress) {
        LogPrintf("CLibernodeBroadcast::Update -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CLibernodeBroadcast::Update -- CheckSignature() failed, libernode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    // if ther was no libernode broadcast recently or if it matches our Libernode privkey...
    if (!pmn->IsBroadcastedWithin(LIBERNODE_MIN_MNB_SECONDS) || (fLiberNode && pubKeyLibernode == activeLibernode.pubKeyLibernode)) {
        // take the newest entry
        LogPrintf("CLibernodeBroadcast::Update -- Got UPDATED Libernode entry: addr=%s\n", addr.ToString());
        if (pmn->UpdateFromNewBroadcast((*this))) {
            pmn->Check();
            RelayLiberNode();
        }
        libernodeSync.AddedLibernodeList();
    }

    return true;
}

bool CLibernodeBroadcast::CheckOutpoint(int &nDos) {
    // we are a libernode with the same vin (i.e. already activated) and this mnb is ours (matches our Libernode privkey)
    // so nothing to do here for us
    if (fLiberNode && vin.prevout == activeLibernode.vin.prevout && pubKeyLibernode == activeLibernode.pubKeyLibernode) {
        return false;
    }

    if (!CheckSignature(nDos)) {
        LogPrintf("CLibernodeBroadcast::CheckOutpoint -- CheckSignature() failed, libernode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            // not mnb fault, let it to be checked again later
            LogPrint("libernode", "CLibernodeBroadcast::CheckOutpoint -- Failed to aquire lock, addr=%s", addr.ToString());
            mnodeman.mapSeenLibernodeBroadcast.erase(GetHash());
            return false;
        }

        CCoins coins;
        if (!pcoinsTip->GetCoins(vin.prevout.hash, coins) ||
            (unsigned int) vin.prevout.n >= coins.vout.size() ||
            coins.vout[vin.prevout.n].IsNull()) {
            LogPrint("libernode", "CLibernodeBroadcast::CheckOutpoint -- Failed to find Libernode UTXO, libernode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (coins.vout[vin.prevout.n].nValue != LIBERNODE_COIN_REQUIRED * COIN) {
            LogPrint("libernode", "CLibernodeBroadcast::CheckOutpoint -- Libernode UTXO should have 25000 ZOI, libernode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
        if (chainActive.Height() - coins.nHeight + 1 < Params().GetConsensus().nLibernodeMinimumConfirmations) {
            LogPrintf("CLibernodeBroadcast::CheckOutpoint -- Libernode UTXO must have at least %d confirmations, libernode=%s\n",
                      Params().GetConsensus().nLibernodeMinimumConfirmations, vin.prevout.ToStringShort());
            // maybe we miss few blocks, let this mnb to be checked again later
            mnodeman.mapSeenLibernodeBroadcast.erase(GetHash());
            return false;
        }
    }

    LogPrint("libernode", "CLibernodeBroadcast::CheckOutpoint -- Libernode UTXO verified\n");

    // make sure the vout that was signed is related to the transaction that spawned the Libernode
    //  - this is expensive, so it's only done once per Libernode
    if (!darkSendSigner.IsVinAssociatedWithPubkey(vin, pubKeyCollateralAddress)) {
        LogPrintf("CLibernodeMan::CheckOutpoint -- Got mismatched pubKeyCollateralAddress and vin\n");
        nDos = 33;
        return false;
    }

    // verify that sig time is legit in past
    // should be at least not earlier than block when 25000 ZOI tx got nLibernodeMinimumConfirmations
    uint256 hashBlock = uint256();
    CTransaction tx2;
    GetTransaction(vin.prevout.hash, tx2, Params().GetConsensus(), hashBlock, true);
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex *pMNIndex = (*mi).second; // block for 25000 ZOI tx -> 1 confirmation
            CBlockIndex *pConfIndex = chainActive[pMNIndex->nHeight + Params().GetConsensus().nLibernodeMinimumConfirmations - 1]; // block where tx got nLibernodeMinimumConfirmations
            if (pConfIndex->GetBlockTime() > sigTime) {
                LogPrintf("CLibernodeBroadcast::CheckOutpoint -- Bad sigTime %d (%d conf block is at %d) for Libernode %s %s\n",
                          sigTime, Params().GetConsensus().nLibernodeMinimumConfirmations, pConfIndex->GetBlockTime(), vin.prevout.ToStringShort(), addr.ToString());
                return false;
            }
        }
    }

    return true;
}

bool CLibernodeBroadcast::Sign(CKey &keyCollateralAddress) {
    std::string strError;
    std::string strMessage;

    sigTime = GetAdjustedTime();

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyLibernode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyCollateralAddress)) {
        LogPrintf("CLibernodeBroadcast::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CLibernodeBroadcast::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CLibernodeBroadcast::CheckSignature(int &nDos) {
    std::string strMessage;
    std::string strError = "";
    nDos = 0;

    strMessage = addr.ToString() + boost::lexical_cast<std::string>(sigTime) +
                 pubKeyCollateralAddress.GetID().ToString() + pubKeyLibernode.GetID().ToString() +
                 boost::lexical_cast<std::string>(nProtocolVersion);

    LogPrint("libernode", "CLibernodeBroadcast::CheckSignature -- strMessage: %s  pubKeyCollateralAddress address: %s  sig: %s\n", strMessage, CBitcoinAddress(pubKeyCollateralAddress.GetID()).ToString(), EncodeBase64(&vchSig[0], vchSig.size()));

    if (!darkSendSigner.VerifyMessage(pubKeyCollateralAddress, vchSig, strMessage, strError)) {
        LogPrintf("CLibernodeBroadcast::CheckSignature -- Got bad Libernode announce signature, error: %s\n", strError);
        nDos = 100;
        return false;
    }

    return true;
}

void CLibernodeBroadcast::RelayLiberNode() {
    LogPrintf("CLibernodeBroadcast::RelayLiberNode\n");
    CInv inv(MSG_LIBERNODE_ANNOUNCE, GetHash());
    RelayInv(inv);
}

CLibernodePing::CLibernodePing(CTxIn &vinNew) {
    LOCK(cs_main);
    if (!chainActive.Tip() || chainActive.Height() < 12) return;

    vin = vinNew;
    blockHash = chainActive[chainActive.Height() - 12]->GetBlockHash();
    sigTime = GetAdjustedTime();
    vchSig = std::vector < unsigned char > ();
}

bool CLibernodePing::Sign(CKey &keyLibernode, CPubKey &pubKeyLibernode) {
    std::string strError;
    std::string strLiberNodeSignMessage;

    sigTime = GetAdjustedTime();
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, keyLibernode)) {
        LogPrintf("CLibernodePing::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(pubKeyLibernode, vchSig, strMessage, strError)) {
        LogPrintf("CLibernodePing::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CLibernodePing::CheckSignature(CPubKey &pubKeyLibernode, int &nDos) {
    std::string strMessage = vin.ToString() + blockHash.ToString() + boost::lexical_cast<std::string>(sigTime);
    std::string strError = "";
    nDos = 0;

    if (!darkSendSigner.VerifyMessage(pubKeyLibernode, vchSig, strMessage, strError)) {
        LogPrintf("CLibernodePing::CheckSignature -- Got bad Libernode ping signature, libernode=%s, error: %s\n", vin.prevout.ToStringShort(), strError);
        nDos = 33;
        return false;
    }
    return true;
}

bool CLibernodePing::SimpleCheck(int &nDos) {
    // don't ban by default
    nDos = 0;

    if (sigTime > GetAdjustedTime() + 60 * 60) {
        LogPrintf("CLibernodePing::SimpleCheck -- Signature rejected, too far into the future, libernode=%s\n", vin.prevout.ToStringShort());
        nDos = 1;
        return false;
    }

    {
//        LOCK(cs_main);
        AssertLockHeld(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if (mi == mapBlockIndex.end()) {
            LogPrint("libernode", "CLibernodePing::SimpleCheck -- Libernode ping is invalid, unknown block hash: libernode=%s blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // maybe we stuck or forked so we shouldn't ban this node, just fail to accept this ping
            // TODO: or should we also request this block?
            return false;
        }
    }
    LogPrint("libernode", "CLibernodePing::SimpleCheck -- Libernode ping verified: libernode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);
    return true;
}

bool CLibernodePing::CheckAndUpdate(CLibernode *pmn, bool fFromNewBroadcast, int &nDos) {
    // don't ban by default
    nDos = 0;

    if (!SimpleCheck(nDos)) {
        return false;
    }

    if (pmn == NULL) {
        LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- Couldn't find Libernode entry, libernode=%s\n", vin.prevout.ToStringShort());
        return false;
    }

    if (!fFromNewBroadcast) {
        if (pmn->IsUpdateRequired()) {
            LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- libernode protocol is outdated, libernode=%s\n", vin.prevout.ToStringShort());
            return false;
        }

        if (pmn->IsNewStartRequired()) {
            LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- libernode is completely expired, new start is required, libernode=%s\n", vin.prevout.ToStringShort());
            return false;
        }
    }

    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(blockHash);
        if ((*mi).second && (*mi).second->nHeight < chainActive.Height() - 24) {
            LogPrintf("CLibernodePing::CheckAndUpdate -- Libernode ping is invalid, block hash is too old: libernode=%s  blockHash=%s\n", vin.prevout.ToStringShort(), blockHash.ToString());
            // nDos = 1;
            return false;
        }
    }

    LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- New ping: libernode=%s  blockHash=%s  sigTime=%d\n", vin.prevout.ToStringShort(), blockHash.ToString(), sigTime);

    // LogPrintf("mnping - Found corresponding mn for vin: %s\n", vin.prevout.ToStringShort());
    // update only if there is no known ping for this libernode or
    // last ping was more then LIBERNODE_MIN_MNP_SECONDS-60 ago comparing to this one
    if (pmn->IsPingedWithin(LIBERNODE_MIN_MNP_SECONDS - 60, sigTime)) {
        LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- Libernode ping arrived too early, libernode=%s\n", vin.prevout.ToStringShort());
        //nDos = 1; //disable, this is happening frequently and causing banned peers
        return false;
    }

    if (!CheckSignature(pmn->pubKeyLibernode, nDos)) return false;

    // so, ping seems to be ok

    // if we are still syncing and there was no known ping for this mn for quite a while
    // (NOTE: assuming that LIBERNODE_EXPIRATION_SECONDS/2 should be enough to finish mn list sync)
    if (!libernodeSync.IsLibernodeListSynced() && !pmn->IsPingedWithin(LIBERNODE_EXPIRATION_SECONDS / 2)) {
        // let's bump sync timeout
        LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- bumping sync timeout, libernode=%s\n", vin.prevout.ToStringShort());
        libernodeSync.AddedLibernodeList();
    }

    // let's store this ping as the last one
    LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- Libernode ping accepted, libernode=%s\n", vin.prevout.ToStringShort());
    pmn->lastPing = *this;

    // and update libernodeman.mapSeenLibernodeBroadcast.lastPing which is probably outdated
    CLibernodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenLibernodeBroadcast.count(hash)) {
        mnodeman.mapSeenLibernodeBroadcast[hash].second.lastPing = *this;
    }

    pmn->Check(true); // force update, ignoring cache
    if (!pmn->IsEnabled()) return false;

    LogPrint("libernode", "CLibernodePing::CheckAndUpdate -- Libernode ping acceepted and relayed, libernode=%s\n", vin.prevout.ToStringShort());
    Relay();

    return true;
}

void CLibernodePing::Relay() {
    CInv inv(MSG_LIBERNODE_PING, GetHash());
    RelayInv(inv);
}

//void CLibernode::AddGovernanceVote(uint256 nGovernanceObjectHash)
//{
//    if(mapGovernanceObjectsVotedOn.count(nGovernanceObjectHash)) {
//        mapGovernanceObjectsVotedOn[nGovernanceObjectHash]++;
//    } else {
//        mapGovernanceObjectsVotedOn.insert(std::make_pair(nGovernanceObjectHash, 1));
//    }
//}

//void CLibernode::RemoveGovernanceObject(uint256 nGovernanceObjectHash)
//{
//    std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.find(nGovernanceObjectHash);
//    if(it == mapGovernanceObjectsVotedOn.end()) {
//        return;
//    }
//    mapGovernanceObjectsVotedOn.erase(it);
//}

void CLibernode::UpdateWatchdogVoteTime() {
    LOCK(cs);
    nTimeLastWatchdogVote = GetTime();
}

/**
*   FLAG GOVERNANCE ITEMS AS DIRTY
*
*   - When libernode come and go on the network, we must flag the items they voted on to recalc it's cached flags
*
*/
//void CLibernode::FlagGovernanceItemsAsDirty()
//{
//    std::vector<uint256> vecDirty;
//    {
//        std::map<uint256, int>::iterator it = mapGovernanceObjectsVotedOn.begin();
//        while(it != mapGovernanceObjectsVotedOn.end()) {
//            vecDirty.push_back(it->first);
//            ++it;
//        }
//    }
//    for(size_t i = 0; i < vecDirty.size(); ++i) {
//        libernodeman.AddDirtyGovernanceObjectHash(vecDirty[i]);
//    }
//}
