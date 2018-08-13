// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activelibernode.h"
#include "darksend.h"
#include "libernode-payments.h"
#include "libernode-sync.h"
#include "libernodeman.h"
#include "netfulfilledman.h"
#include "spork.h"
#include "util.h"

#include <boost/lexical_cast.hpp>

/** Object for who's going to get paid on which blocks */
CLibernodePayments mnpayments;

CCriticalSection cs_vecPayees;
CCriticalSection cs_mapLibernodeBlocks;
CCriticalSection cs_mapLibernodePaymentVotes;

/**
* IsBlockValueValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Why is this needed?
*   - In Dash some blocks are superblocks, which output much higher amounts of coins
*   - Otherblocks are 10% lower in outgoing value, so in total, no extra coins are created
*   - When non-superblocks are detected, the normal schedule should be maintained
*/

bool IsBlockValueValid(const CBlock &block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet) {
    strErrorRet = "";

    bool isBlockRewardValueMet = (block.vtx[0].GetValueOut() <= blockReward);
    if (fDebug) LogPrintf("block.vtx[0].GetValueOut() %lld <= blockReward %lld\n", block.vtx[0].GetValueOut(), blockReward);

    // we are still using budgets, but we have no data about them anymore,
    // all we know is predefined budget cycle and window

//    const Consensus::Params &consensusParams = Params().GetConsensus();
//
////    if (nBlockHeight < consensusParams.nSuperblockStartBlock) {
//        int nOffset = nBlockHeight % consensusParams.nBudgetPaymentsCycleBlocks;
//        if (nBlockHeight >= consensusParams.nBudgetPaymentsStartBlock &&
//            nOffset < consensusParams.nBudgetPaymentsWindowBlocks) {
//            // NOTE: make sure SPORK_13_OLD_SUPERBLOCK_FLAG is disabled when 12.1 starts to go live
//            if (libernodeSync.IsSynced() && !sporkManager.IsSporkActive(SPORK_13_OLD_SUPERBLOCK_FLAG)) {
//                // no budget blocks should be accepted here, if SPORK_13_OLD_SUPERBLOCK_FLAG is disabled
//                LogPrint("gobject", "IsBlockValueValid -- Client synced but budget spork is disabled, checking block value against block reward\n");
//                if (!isBlockRewardValueMet) {
//                    strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, budgets are disabled",
//                                            nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//                }
//                return isBlockRewardValueMet;
//            }
//            LogPrint("gobject", "IsBlockValueValid -- WARNING: Skipping budget block value checks, accepting block\n");
//            // TODO: reprocess blocks to make sure they are legit?
//            return true;
//        }
//        // LogPrint("gobject", "IsBlockValueValid -- Block is not in budget cycle window, checking block value against block reward\n");
//        if (!isBlockRewardValueMet) {
//            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, block is not in budget cycle window",
//                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//        }
//        return isBlockRewardValueMet;
//    }

    // superblocks started

//    CAmount nSuperblockMaxValue =  blockReward + CSuperblock::GetPaymentsLimit(nBlockHeight);
//    bool isSuperblockMaxValueMet = (block.vtx[0].GetValueOut() <= nSuperblockMaxValue);
//    bool isSuperblockMaxValueMet = false;

//    LogPrint("gobject", "block.vtx[0].GetValueOut() %lld <= nSuperblockMaxValue %lld\n", block.vtx[0].GetValueOut(), nSuperblockMaxValue);

    if (!libernodeSync.IsSynced()) {
        // not enough data but at least it must NOT exceed superblock max value
//        if(CSuperblock::IsValidBlockHeight(nBlockHeight)) {
//            if(fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, checking superblock max bounds only\n");
//            if(!isSuperblockMaxValueMet) {
//                strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded superblock max value",
//                                        nBlockHeight, block.vtx[0].GetValueOut(), nSuperblockMaxValue);
//            }
//            return isSuperblockMaxValueMet;
//        }
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, only regular blocks are allowed at this height",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
        // it MUST be a regular block otherwise
        return isBlockRewardValueMet;
    }

    // we are synced, let's try to check as much data as we can

    if (sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED)) {
////        if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
////            if(CSuperblockManager::IsValid(block.vtx[0], nBlockHeight, blockReward)) {
////                LogPrint("gobject", "IsBlockValueValid -- Valid superblock at height %d: %s", nBlockHeight, block.vtx[0].ToString());
////                // all checks are done in CSuperblock::IsValid, nothing to do here
////                return true;
////            }
////
////            // triggered but invalid? that's weird
////            LogPrintf("IsBlockValueValid -- ERROR: Invalid superblock detected at height %d: %s", nBlockHeight, block.vtx[0].ToString());
////            // should NOT allow invalid superblocks, when superblocks are enabled
////            strErrorRet = strprintf("invalid superblock detected at height %d", nBlockHeight);
////            return false;
////        }
//        LogPrint("gobject", "IsBlockValueValid -- No triggered superblock detected at height %d\n", nBlockHeight);
//        if(!isBlockRewardValueMet) {
//            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, no triggered superblock detected",
//                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
//        }
    } else {
//        // should NOT allow superblocks at all, when superblocks are disabled
        LogPrint("gobject", "IsBlockValueValid -- Superblocks are disabled, no superblocks allowed\n");
        if (!isBlockRewardValueMet) {
            strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, superblocks are disabled",
                                    nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        }
    }

    // it MUST be a regular block
    return isBlockRewardValueMet;
}

bool IsBlockPayeeValid(const CTransaction &txNew, int nBlockHeight, CAmount blockReward) {
    // we can only check libernode payment /
    const Consensus::Params &consensusParams = Params().GetConsensus();

    if (nBlockHeight < consensusParams.nLibernodePaymentsStartBlock) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsBlockPayeeValid -- libernode isn't start\n");
        return true;
    }
    if (!libernodeSync.IsSynced()) {
        //there is no budget data to use to check anything, let's just accept the longest chain
        if (fDebug) LogPrintf("IsBlockPayeeValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    //check for libernode payee
    if (mnpayments.IsTransactionValid(txNew, nBlockHeight)) {
        LogPrint("mnpayments", "IsBlockPayeeValid -- Valid libernode payment at height %d: %s", nBlockHeight, txNew.ToString());
        return true;
    } else {
        if(sporkManager.IsSporkActive(SPORK_8_LIBERNODE_PAYMENT_ENFORCEMENT)){
            return false;
        } else {
            LogPrintf("LiberNode payment enforcement is disabled, accepting block\n");
            return true;
        }
    }
}

void FillBlockPayments(CMutableTransaction &txNew, int nBlockHeight, CAmount libernodePayment, CTxOut &txoutLibernodeRet, std::vector <CTxOut> &voutSuperblockRet) {
    // only create superblocks if spork is enabled AND if superblock is actually triggered
    // (height should be validated inside)
//    if(sporkManager.IsSporkActive(SPORK_9_SUPERBLOCKS_ENABLED) &&
//        CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
//            LogPrint("gobject", "FillBlockPayments -- triggered superblock creation at height %d\n", nBlockHeight);
//            CSuperblockManager::CreateSuperblock(txNew, nBlockHeight, voutSuperblockRet);
//            return;
//    }

    // FILL BLOCK PAYEE WITH LIBERNODE PAYMENT OTHERWISE
    mnpayments.FillBlockPayee(txNew, nBlockHeight, libernodePayment, txoutLibernodeRet);
    LogPrint("mnpayments", "FillBlockPayments -- nBlockHeight %d libernodePayment %lld txoutLibernodeRet %s txNew %s",
             nBlockHeight, libernodePayment, txoutLibernodeRet.ToString(), txNew.ToString());
}

std::string GetRequiredPaymentsString(int nBlockHeight) {
    // IF WE HAVE A ACTIVATED TRIGGER FOR THIS HEIGHT - IT IS A SUPERBLOCK, GET THE REQUIRED PAYEES
//    if(CSuperblockManager::IsSuperblockTriggered(nBlockHeight)) {
//        return CSuperblockManager::GetRequiredPaymentsString(nBlockHeight);
//    }

    // OTHERWISE, PAY LIBERNODE
    return mnpayments.GetRequiredPaymentsString(nBlockHeight);
}

void CLibernodePayments::Clear() {
    LOCK2(cs_mapLibernodeBlocks, cs_mapLibernodePaymentVotes);
    mapLibernodeBlocks.clear();
    mapLibernodePaymentVotes.clear();
}

bool CLibernodePayments::CanVote(COutPoint outLibernode, int nBlockHeight) {
    LOCK(cs_mapLibernodePaymentVotes);

    if (mapLibernodesLastVote.count(outLibernode) && mapLibernodesLastVote[outLibernode] == nBlockHeight) {
        return false;
    }

    //record this libernode voted
    mapLibernodesLastVote[outLibernode] = nBlockHeight;
    return true;
}

std::string CLibernodePayee::ToString() const {
    CTxDestination address1;
    ExtractDestination(scriptPubKey, address1);
    CBitcoinAddress address2(address1);
    std::string str;
    str += "(address: ";
    str += address2.ToString();
    str += ")\n";
    return str;
}

/**
*   FillBlockPayee
*
*   Fill Libernode ONLY payment block
*/

void CLibernodePayments::FillBlockPayee(CMutableTransaction &txNew, int nBlockHeight, CAmount libernodePayment, CTxOut &txoutLibernodeRet) {
    // make sure it's not filled yet
    txoutLibernodeRet = CTxOut();

    CScript payee;
    bool foundMaxVotedPayee = true;

    if (!mnpayments.GetBlockPayee(nBlockHeight, payee)) {
        // no libernode detected...
        // LogPrintf("no libernode detected...\n");
        foundMaxVotedPayee = false;
        int nCount = 0;
        CLibernode *winningNode = mnodeman.GetNextLibernodeInQueueForPayment(nBlockHeight, true, nCount);
        if (!winningNode) {
            // ...and we can't calculate it on our own
            LogPrintf("CLibernodePayments::FillBlockPayee -- Failed to detect libernode to pay\n");
            return;
        }
        // fill payee with locally calculated winner and hope for the best
        payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        LogPrintf("payee=%s\n", winningNode->ToString());
    }
    txoutLibernodeRet = CTxOut(libernodePayment, payee);
    txNew.vout.push_back(txoutLibernodeRet);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);
    if (foundMaxVotedPayee) {
        LogPrintf("CLibernodePayments::FillBlockPayee::foundMaxVotedPayee -- Libernode payment %lld to %s\n", libernodePayment, address2.ToString());
    } else {
        LogPrintf("CLibernodePayments::FillBlockPayee -- Libernode payment %lld to %s\n", libernodePayment, address2.ToString());
    }

}

int CLibernodePayments::GetMinLibernodePaymentsProto() {
    return sporkManager.IsSporkActive(SPORK_10_LIBERNODE_PAY_UPDATED_NODES)
           ? MIN_LIBERNODE_PAYMENT_PROTO_VERSION_2
           : MIN_LIBERNODE_PAYMENT_PROTO_VERSION_1;
}

void CLibernodePayments::ProcessMessage(CNode *pfrom, std::string &strCommand, CDataStream &vRecv) {

//    LogPrintf("CLibernodePayments::ProcessMessage strCommand=%s\n", strCommand);
    // Ignore any payments messages until libernode list is synced
    if (!libernodeSync.IsLibernodeListSynced()) return;

    if (fLiteMode) return; // disable all Dash specific functionality

    if (strCommand == NetMsgType::LIBERNODEPAYMENTSYNC) { //Libernode Payments Request Sync

        // Ignore such requests until we are fully synced.
        // We could start processing this after libernode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!libernodeSync.IsSynced()) return;

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (netfulfilledman.HasFulfilledRequest(pfrom->addr, NetMsgType::LIBERNODEPAYMENTSYNC)) {
            // Asking for the payments list multiple times in a short period of time is no good
            LogPrintf("LIBERNODEPAYMENTSYNC -- peer already asked me for the list, peer=%d\n", pfrom->id);
            Misbehaving(pfrom->GetId(), 20);
            return;
        }
        netfulfilledman.AddFulfilledRequest(pfrom->addr, NetMsgType::LIBERNODEPAYMENTSYNC);

        Sync(pfrom);
        LogPrint("mnpayments", "LIBERNODEPAYMENTSYNC -- Sent Libernode payment votes to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::LIBERNODEPAYMENTVOTE) { // Libernode Payments Vote for the Winner

        CLibernodePaymentVote vote;
        vRecv >> vote;

        if (pfrom->nVersion < GetMinLibernodePaymentsProto()) return;

        if (!pCurrentBlockIndex) return;

        uint256 nHash = vote.GetHash();

        pfrom->setAskFor.erase(nHash);

        {
            LOCK(cs_mapLibernodePaymentVotes);
            if (mapLibernodePaymentVotes.count(nHash)) {
                LogPrint("mnpayments", "LIBERNODEPAYMENTVOTE -- hash=%s, nHeight=%d seen\n", nHash.ToString(), pCurrentBlockIndex->nHeight);
                return;
            }

            // Avoid processing same vote multiple times
            mapLibernodePaymentVotes[nHash] = vote;
            // but first mark vote as non-verified,
            // AddPaymentVote() below should take care of it if vote is actually ok
            mapLibernodePaymentVotes[nHash].MarkAsNotVerified();
        }

        int nFirstBlock = pCurrentBlockIndex->nHeight - GetStorageLimit();
        if (vote.nBlockHeight < nFirstBlock || vote.nBlockHeight > pCurrentBlockIndex->nHeight + 20) {
            LogPrint("mnpayments", "LIBERNODEPAYMENTVOTE -- vote out of range: nFirstBlock=%d, nBlockHeight=%d, nHeight=%d\n", nFirstBlock, vote.nBlockHeight, pCurrentBlockIndex->nHeight);
            return;
        }

        std::string strError = "";
        if (!vote.IsValid(pfrom, pCurrentBlockIndex->nHeight, strError)) {
            LogPrint("mnpayments", "LIBERNODEPAYMENTVOTE -- invalid message, error: %s\n", strError);
            return;
        }

        if (!CanVote(vote.vinLibernode.prevout, vote.nBlockHeight)) {
            LogPrintf("LIBERNODEPAYMENTVOTE -- libernode already voted, libernode=%s\n", vote.vinLibernode.prevout.ToStringShort());
            return;
        }

        libernode_info_t mnInfo = mnodeman.GetLibernodeInfo(vote.vinLibernode);
        if (!mnInfo.fInfoValid) {
            // mn was not found, so we can't check vote, some info is probably missing
            LogPrintf("LIBERNODEPAYMENTVOTE -- libernode is missing %s\n", vote.vinLibernode.prevout.ToStringShort());
            mnodeman.AskForMN(pfrom, vote.vinLibernode);
            return;
        }

        int nDos = 0;
        if (!vote.CheckSignature(mnInfo.pubKeyLibernode, pCurrentBlockIndex->nHeight, nDos)) {
            if (nDos) {
                LogPrintf("LIBERNODEPAYMENTVOTE -- ERROR: invalid signature\n");
                Misbehaving(pfrom->GetId(), nDos);
            } else {
                // only warn about anything non-critical (i.e. nDos == 0) in debug mode
                LogPrint("mnpayments", "LIBERNODEPAYMENTVOTE -- WARNING: invalid signature\n");
            }
            // Either our info or vote info could be outdated.
            // In case our info is outdated, ask for an update,
            mnodeman.AskForMN(pfrom, vote.vinLibernode);
            // but there is nothing we can do if vote info itself is outdated
            // (i.e. it was signed by a mn which changed its key),
            // so just quit here.
            return;
        }

        CTxDestination address1;
        ExtractDestination(vote.payee, address1);
        CBitcoinAddress address2(address1);

        LogPrint("mnpayments", "LIBERNODEPAYMENTVOTE -- vote: address=%s, nBlockHeight=%d, nHeight=%d, prevout=%s\n", address2.ToString(), vote.nBlockHeight, pCurrentBlockIndex->nHeight, vote.vinLibernode.prevout.ToStringShort());

        if (AddPaymentVote(vote)) {
            vote.Relay();
            libernodeSync.AddedPaymentVote();
        }
    }
}

bool CLibernodePaymentVote::Sign() {
    std::string strError;
    std::string strMessage = vinLibernode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    if (!darkSendSigner.SignMessage(strMessage, vchSig, activeLibernode.keyLibernode)) {
        LogPrintf("CLibernodePaymentVote::Sign -- SignMessage() failed\n");
        return false;
    }

    if (!darkSendSigner.VerifyMessage(activeLibernode.pubKeyLibernode, vchSig, strMessage, strError)) {
        LogPrintf("CLibernodePaymentVote::Sign -- VerifyMessage() failed, error: %s\n", strError);
        return false;
    }

    return true;
}

bool CLibernodePayments::GetBlockPayee(int nBlockHeight, CScript &payee) {
    if (mapLibernodeBlocks.count(nBlockHeight)) {
        return mapLibernodeBlocks[nBlockHeight].GetBestPayee(payee);
    }

    return false;
}

// Is this libernode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 blocks of votes
bool CLibernodePayments::IsScheduled(CLibernode &mn, int nNotBlockHeight) {
    LOCK(cs_mapLibernodeBlocks);

    if (!pCurrentBlockIndex) return false;

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = pCurrentBlockIndex->nHeight; h <= pCurrentBlockIndex->nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapLibernodeBlocks.count(h) && mapLibernodeBlocks[h].GetBestPayee(payee) && mnpayee == payee) {
            return true;
        }
    }

    return false;
}

bool CLibernodePayments::AddPaymentVote(const CLibernodePaymentVote &vote) {
    LogPrint("libernode-payments", "CLibernodePayments::AddPaymentVote\n");
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, vote.nBlockHeight - 119)) return false;

    if (HasVerifiedPaymentVote(vote.GetHash())) return false;

    LOCK2(cs_mapLibernodeBlocks, cs_mapLibernodePaymentVotes);

    mapLibernodePaymentVotes[vote.GetHash()] = vote;

    if (!mapLibernodeBlocks.count(vote.nBlockHeight)) {
        CLibernodeBlockPayees blockPayees(vote.nBlockHeight);
        mapLibernodeBlocks[vote.nBlockHeight] = blockPayees;
    }

    mapLibernodeBlocks[vote.nBlockHeight].AddPayee(vote);

    return true;
}

bool CLibernodePayments::HasVerifiedPaymentVote(uint256 hashIn) {
    LOCK(cs_mapLibernodePaymentVotes);
    std::map<uint256, CLibernodePaymentVote>::iterator it = mapLibernodePaymentVotes.find(hashIn);
    return it != mapLibernodePaymentVotes.end() && it->second.IsVerified();
}

void CLibernodeBlockPayees::AddPayee(const CLibernodePaymentVote &vote) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CLibernodePayee & payee, vecPayees)
    {
        if (payee.GetPayee() == vote.payee) {
            payee.AddVoteHash(vote.GetHash());
            return;
        }
    }
    CLibernodePayee payeeNew(vote.payee, vote.GetHash());
    vecPayees.push_back(payeeNew);
}

bool CLibernodeBlockPayees::GetBestPayee(CScript &payeeRet) {
    LOCK(cs_vecPayees);
    LogPrint("mnpayments", "CLibernodeBlockPayees::GetBestPayee, vecPayees.size()=%s\n", vecPayees.size());
    if (!vecPayees.size()) {
        LogPrint("mnpayments", "CLibernodeBlockPayees::GetBestPayee -- ERROR: couldn't find any payee\n");
        return false;
    }

    int nVotes = -1;
    BOOST_FOREACH(CLibernodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() > nVotes) {
            payeeRet = payee.GetPayee();
            nVotes = payee.GetVoteCount();
        }
    }

    return (nVotes > -1);
}

bool CLibernodeBlockPayees::HasPayeeWithVotes(CScript payeeIn, int nVotesReq) {
    LOCK(cs_vecPayees);

    BOOST_FOREACH(CLibernodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nVotesReq && payee.GetPayee() == payeeIn) {
            return true;
        }
    }

//    LogPrint("mnpayments", "CLibernodeBlockPayees::HasPayeeWithVotes -- ERROR: couldn't find any payee with %d+ votes\n", nVotesReq);
    return false;
}

bool CLibernodeBlockPayees::IsTransactionValid(const CTransaction &txNew) {
    LOCK(cs_vecPayees);

    int nMaxSignatures = 0;
    std::string strPayeesPossible = "";

    CAmount nLibernodePayment = GetLibernodePayment(nBlockHeight, txNew.GetValueOut());

    //require at least MNPAYMENTS_SIGNATURES_REQUIRED signatures

    BOOST_FOREACH(CLibernodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= nMaxSignatures) {
            nMaxSignatures = payee.GetVoteCount();
        }
    }

    // if we don't have at least MNPAYMENTS_SIGNATURES_REQUIRED signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    bool hasValidPayee = false;

    BOOST_FOREACH(CLibernodePayee & payee, vecPayees)
    {
        if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            hasValidPayee = true;

            BOOST_FOREACH(CTxOut txout, txNew.vout) {
                if (payee.GetPayee() == txout.scriptPubKey && nLibernodePayment == txout.nValue) {
                    LogPrint("mnpayments", "CLibernodeBlockPayees::IsTransactionValid -- Found required payment\n");
                    return true;
                }
            }

            CTxDestination address1;
            ExtractDestination(payee.GetPayee(), address1);
            CBitcoinAddress address2(address1);

            if (strPayeesPossible == "") {
                strPayeesPossible = address2.ToString();
            } else {
                strPayeesPossible += "," + address2.ToString();
            }
        }
    }

    if (!hasValidPayee) return true;

    LogPrintf("CLibernodeBlockPayees::IsTransactionValid -- ERROR: Missing required payment, possible payees: '%s', amount: %f ZOI\n", strPayeesPossible, (float) nLibernodePayment / COIN);
    return false;
}

std::string CLibernodeBlockPayees::GetRequiredPaymentsString() {
    LOCK(cs_vecPayees);

    std::string strRequiredPayments = "Unknown";

    BOOST_FOREACH(CLibernodePayee & payee, vecPayees)
    {
        CTxDestination address1;
        ExtractDestination(payee.GetPayee(), address1);
        CBitcoinAddress address2(address1);

        if (strRequiredPayments != "Unknown") {
            strRequiredPayments += ", " + address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        } else {
            strRequiredPayments = address2.ToString() + ":" + boost::lexical_cast<std::string>(payee.GetVoteCount());
        }
    }

    return strRequiredPayments;
}

std::string CLibernodePayments::GetRequiredPaymentsString(int nBlockHeight) {
    LOCK(cs_mapLibernodeBlocks);

    if (mapLibernodeBlocks.count(nBlockHeight)) {
        return mapLibernodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CLibernodePayments::IsTransactionValid(const CTransaction &txNew, int nBlockHeight) {
    LOCK(cs_mapLibernodeBlocks);

    if (mapLibernodeBlocks.count(nBlockHeight)) {
        return mapLibernodeBlocks[nBlockHeight].IsTransactionValid(txNew);
    }

    return true;
}

void CLibernodePayments::CheckAndRemove() {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_mapLibernodeBlocks, cs_mapLibernodePaymentVotes);

    int nLimit = GetStorageLimit();

    std::map<uint256, CLibernodePaymentVote>::iterator it = mapLibernodePaymentVotes.begin();
    while (it != mapLibernodePaymentVotes.end()) {
        CLibernodePaymentVote vote = (*it).second;

        if (pCurrentBlockIndex->nHeight - vote.nBlockHeight > nLimit) {
            LogPrint("mnpayments", "CLibernodePayments::CheckAndRemove -- Removing old Libernode payment: nBlockHeight=%d\n", vote.nBlockHeight);
            mapLibernodePaymentVotes.erase(it++);
            mapLibernodeBlocks.erase(vote.nBlockHeight);
        } else {
            ++it;
        }
    }
    LogPrintf("CLibernodePayments::CheckAndRemove -- %s\n", ToString());
}

bool CLibernodePaymentVote::IsValid(CNode *pnode, int nValidationHeight, std::string &strError) {
    CLibernode *pmn = mnodeman.Find(vinLibernode);

    if (!pmn) {
        strError = strprintf("Unknown Libernode: prevout=%s", vinLibernode.prevout.ToStringShort());
        // Only ask if we are already synced and still have no idea about that Libernode
        if (libernodeSync.IsLibernodeListSynced()) {
            mnodeman.AskForMN(pnode, vinLibernode);
        }

        return false;
    }

    int nMinRequiredProtocol;
    if (nBlockHeight >= nValidationHeight) {
        // new votes must comply SPORK_10_LIBERNODE_PAY_UPDATED_NODES rules
        nMinRequiredProtocol = mnpayments.GetMinLibernodePaymentsProto();
    } else {
        // allow non-updated libernodes for old blocks
        nMinRequiredProtocol = MIN_LIBERNODE_PAYMENT_PROTO_VERSION_1;
    }

    if (pmn->nProtocolVersion < nMinRequiredProtocol) {
        strError = strprintf("Libernode protocol is too old: nProtocolVersion=%d, nMinRequiredProtocol=%d", pmn->nProtocolVersion, nMinRequiredProtocol);
        return false;
    }

    // Only libernodes should try to check libernode rank for old votes - they need to pick the right winner for future blocks.
    // Regular clients (miners included) need to verify libernode rank for future block votes only.
    if (!fLiberNode && nBlockHeight < nValidationHeight) return true;

    int nRank = mnodeman.GetLibernodeRank(vinLibernode, nBlockHeight - 119, nMinRequiredProtocol, false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CLibernodePaymentVote::IsValid -- Can't calculate rank for libernode %s\n",
                 vinLibernode.prevout.ToStringShort());
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        // It's common to have libernodes mistakenly think they are in the top 10
        // We don't want to print all of these messages in normal mode, debug mode should print though
        strError = strprintf("Libernode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        // Only ban for new mnw which is out of bounds, for old mnw MN list itself might be way too much off
        if (nRank > MNPAYMENTS_SIGNATURES_TOTAL * 2 && nBlockHeight > nValidationHeight) {
            strError = strprintf("Libernode is not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, nRank);
            LogPrintf("CLibernodePaymentVote::IsValid -- Error: %s\n", strError);
            Misbehaving(pnode->GetId(), 20);
        }
        // Still invalid however
        return false;
    }

    return true;
}

bool CLibernodePayments::ProcessBlock(int nBlockHeight) {

    // DETERMINE IF WE SHOULD BE VOTING FOR THE NEXT PAYEE

    if (fLiteMode || !fLiberNode) {
        return false;
    }

    // We have little chances to pick the right winner if winners list is out of sync
    // but we have no choice, so we'll try. However it doesn't make sense to even try to do so
    // if we have not enough data about libernodes.
    if (!libernodeSync.IsLibernodeListSynced()) {
        return false;
    }

    int nRank = mnodeman.GetLibernodeRank(activeLibernode.vin, nBlockHeight - 119, GetMinLibernodePaymentsProto(), false);

    if (nRank == -1) {
        LogPrint("mnpayments", "CLibernodePayments::ProcessBlock -- Unknown Libernode\n");
        return false;
    }

    if (nRank > MNPAYMENTS_SIGNATURES_TOTAL) {
        LogPrint("mnpayments", "CLibernodePayments::ProcessBlock -- Libernode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, nRank);
        return false;
    }

    // LOCATE THE NEXT LIBERNODE WHICH SHOULD BE PAID

    LogPrintf("CLibernodePayments::ProcessBlock -- Start: nBlockHeight=%d, libernode=%s\n", nBlockHeight, activeLibernode.vin.prevout.ToStringShort());

    // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
    int nCount = 0;
    CLibernode *pmn = mnodeman.GetNextLibernodeInQueueForPayment(nBlockHeight, true, nCount);

    if (pmn == NULL) {
        LogPrintf("CLibernodePayments::ProcessBlock -- ERROR: Failed to find libernode to pay\n");
        return false;
    }

    LogPrintf("CLibernodePayments::ProcessBlock -- Libernode found by GetNextLibernodeInQueueForPayment(): %s\n", pmn->vin.prevout.ToStringShort());


    CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());

    CLibernodePaymentVote voteNew(activeLibernode.vin, nBlockHeight, payee);

    CTxDestination address1;
    ExtractDestination(payee, address1);
    CBitcoinAddress address2(address1);

    // SIGN MESSAGE TO NETWORK WITH OUR LIBERNODE KEYS

    if (voteNew.Sign()) {
        if (AddPaymentVote(voteNew)) {
            voteNew.Relay();
            return true;
        }
    }

    return false;
}

void CLibernodePaymentVote::Relay() {
    // do not relay until synced
    if (!libernodeSync.IsWinnersListSynced()) {
        LogPrintf("CLibernodePaymentVote::Relay - libernodeSync.IsWinnersListSynced() not sync\n");
        return;
    }
    CInv inv(MSG_LIBERNODE_PAYMENT_VOTE, GetHash());
    RelayInv(inv);
}

bool CLibernodePaymentVote::CheckSignature(const CPubKey &pubKeyLibernode, int nValidationHeight, int &nDos) {
    // do not ban by default
    nDos = 0;

    std::string strMessage = vinLibernode.prevout.ToStringShort() +
                             boost::lexical_cast<std::string>(nBlockHeight) +
                             ScriptToAsmStr(payee);

    std::string strError = "";
    if (!darkSendSigner.VerifyMessage(pubKeyLibernode, vchSig, strMessage, strError)) {
        // Only ban for future block vote when we are already synced.
        // Otherwise it could be the case when MN which signed this vote is using another key now
        // and we have no idea about the old one.
        if (libernodeSync.IsLibernodeListSynced() && nBlockHeight > nValidationHeight) {
            nDos = 20;
        }
        return error("CLibernodePaymentVote::CheckSignature -- Got bad Libernode payment signature, libernode=%s, error: %s", vinLibernode.prevout.ToStringShort().c_str(), strError);
    }

    return true;
}

std::string CLibernodePaymentVote::ToString() const {
    std::ostringstream info;

    info << vinLibernode.prevout.ToStringShort() <<
         ", " << nBlockHeight <<
         ", " << ScriptToAsmStr(payee) <<
         ", " << (int) vchSig.size();

    return info.str();
}

// Send only votes for future blocks, node should request every other missing payment block individually
void CLibernodePayments::Sync(CNode *pnode) {
    LOCK(cs_mapLibernodeBlocks);

    if (!pCurrentBlockIndex) return;

    int nInvCount = 0;

    for (int h = pCurrentBlockIndex->nHeight; h < pCurrentBlockIndex->nHeight + 20; h++) {
        if (mapLibernodeBlocks.count(h)) {
            BOOST_FOREACH(CLibernodePayee & payee, mapLibernodeBlocks[h].vecPayees)
            {
                std::vector <uint256> vecVoteHashes = payee.GetVoteHashes();
                BOOST_FOREACH(uint256 & hash, vecVoteHashes)
                {
                    if (!HasVerifiedPaymentVote(hash)) continue;
                    pnode->PushInventory(CInv(MSG_LIBERNODE_PAYMENT_VOTE, hash));
                    nInvCount++;
                }
            }
        }
    }

    LogPrintf("CLibernodePayments::Sync -- Sent %d votes to peer %d\n", nInvCount, pnode->id);
    pnode->PushMessage(NetMsgType::SYNCSTATUSCOUNT, LIBERNODE_SYNC_MNW, nInvCount);
}

// Request low data/unknown payment blocks in batches directly from some node instead of/after preliminary Sync.
void CLibernodePayments::RequestLowDataPaymentBlocks(CNode *pnode) {
    if (!pCurrentBlockIndex) return;

    LOCK2(cs_main, cs_mapLibernodeBlocks);

    std::vector <CInv> vToFetch;
    int nLimit = GetStorageLimit();

    const CBlockIndex *pindex = pCurrentBlockIndex;

    while (pCurrentBlockIndex->nHeight - pindex->nHeight < nLimit) {
        if (!mapLibernodeBlocks.count(pindex->nHeight)) {
            // We have no idea about this block height, let's ask
            vToFetch.push_back(CInv(MSG_LIBERNODE_PAYMENT_BLOCK, pindex->GetBlockHash()));
            // We should not violate GETDATA rules
            if (vToFetch.size() == MAX_INV_SZ) {
                LogPrintf("CLibernodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d blocks\n", pnode->id, MAX_INV_SZ);
                pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
                // Start filling new batch
                vToFetch.clear();
            }
        }
        if (!pindex->pprev) break;
        pindex = pindex->pprev;
    }

    std::map<int, CLibernodeBlockPayees>::iterator it = mapLibernodeBlocks.begin();

    while (it != mapLibernodeBlocks.end()) {
        int nTotalVotes = 0;
        bool fFound = false;
        BOOST_FOREACH(CLibernodePayee & payee, it->second.vecPayees)
        {
            if (payee.GetVoteCount() >= MNPAYMENTS_SIGNATURES_REQUIRED) {
                fFound = true;
                break;
            }
            nTotalVotes += payee.GetVoteCount();
        }
        // A clear winner (MNPAYMENTS_SIGNATURES_REQUIRED+ votes) was found
        // or no clear winner was found but there are at least avg number of votes
        if (fFound || nTotalVotes >= (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2) {
            // so just move to the next block
            ++it;
            continue;
        }
        // DEBUG
//        DBG (
//            // Let's see why this failed
//            BOOST_FOREACH(CLibernodePayee& payee, it->second.vecPayees) {
//                CTxDestination address1;
//                ExtractDestination(payee.GetPayee(), address1);
//                CBitcoinAddress address2(address1);
//                printf("payee %s votes %d\n", address2.ToString().c_str(), payee.GetVoteCount());
//            }
//            printf("block %d votes total %d\n", it->first, nTotalVotes);
//        )
        // END DEBUG
        // Low data block found, let's try to sync it
        uint256 hash;
        if (GetBlockHash(hash, it->first)) {
            vToFetch.push_back(CInv(MSG_LIBERNODE_PAYMENT_BLOCK, hash));
        }
        // We should not violate GETDATA rules
        if (vToFetch.size() == MAX_INV_SZ) {
            LogPrintf("CLibernodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, MAX_INV_SZ);
            pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
            // Start filling new batch
            vToFetch.clear();
        }
        ++it;
    }
    // Ask for the rest of it
    if (!vToFetch.empty()) {
        LogPrintf("CLibernodePayments::SyncLowDataPaymentBlocks -- asking peer %d for %d payment blocks\n", pnode->id, vToFetch.size());
        pnode->PushMessage(NetMsgType::GETDATA, vToFetch);
    }
}

std::string CLibernodePayments::ToString() const {
    std::ostringstream info;

    info << "Votes: " << (int) mapLibernodePaymentVotes.size() <<
         ", Blocks: " << (int) mapLibernodeBlocks.size();

    return info.str();
}

bool CLibernodePayments::IsEnoughData() {
    float nAverageVotes = (MNPAYMENTS_SIGNATURES_TOTAL + MNPAYMENTS_SIGNATURES_REQUIRED) / 2;
    int nStorageLimit = GetStorageLimit();
    return GetBlockCount() > nStorageLimit && GetVoteCount() > nStorageLimit * nAverageVotes;
}

int CLibernodePayments::GetStorageLimit() {
    return std::max(int(mnodeman.size() * nStorageCoeff), nMinBlocksToStore);
}

void CLibernodePayments::UpdatedBlockTip(const CBlockIndex *pindex) {
    pCurrentBlockIndex = pindex;
    LogPrint("mnpayments", "CLibernodePayments::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);
    
    ProcessBlock(pindex->nHeight + 5);
}
