// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activelibernode.h"
#include "addrman.h"
#include "darksend.h"
//#include "governance.h"
#include "libernode-payments.h"
#include "libernode-sync.h"
#include "libernodeman.h"
#include "netfulfilledman.h"
#include "util.h"
//#include "random.h"

/** Libernode manager */
CLibernodeMan mnodeman;

const std::string CLibernodeMan::SERIALIZATION_VERSION_STRING = "CLibernodeMan-Version-4";

struct CompareLastPaidBlock
{
    bool operator()(const std::pair<int, CLibernode*>& t1,
                    const std::pair<int, CLibernode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

struct CompareScoreMN
{
    bool operator()(const std::pair<int64_t, CLibernode*>& t1,
                    const std::pair<int64_t, CLibernode*>& t2) const
    {
        return (t1.first != t2.first) ? (t1.first < t2.first) : (t1.second->vin < t2.second->vin);
    }
};

CLibernodeIndex::CLibernodeIndex()
    : nSize(0),
      mapIndex(),
      mapReverseIndex()
{}

bool CLibernodeIndex::Get(int nIndex, CTxIn& vinLibernode) const
{
    rindex_m_cit it = mapReverseIndex.find(nIndex);
    if(it == mapReverseIndex.end()) {
        return false;
    }
    vinLibernode = it->second;
    return true;
}

int CLibernodeIndex::GetLibernodeIndex(const CTxIn& vinLibernode) const
{
    index_m_cit it = mapIndex.find(vinLibernode);
    if(it == mapIndex.end()) {
        return -1;
    }
    return it->second;
}

void CLibernodeIndex::AddLibernodeVIN(const CTxIn& vinLibernode)
{
    index_m_it it = mapIndex.find(vinLibernode);
    if(it != mapIndex.end()) {
        return;
    }
    int nNextIndex = nSize;
    mapIndex[vinLibernode] = nNextIndex;
    mapReverseIndex[nNextIndex] = vinLibernode;
    ++nSize;
}

void CLibernodeIndex::Clear()
{
    mapIndex.clear();
    mapReverseIndex.clear();
    nSize = 0;
}
struct CompareByAddr

{
    bool operator()(const CLibernode* t1,
                    const CLibernode* t2) const
    {
        return t1->addr < t2->addr;
    }
};

void CLibernodeIndex::RebuildIndex()
{
    nSize = mapIndex.size();
    for(index_m_it it = mapIndex.begin(); it != mapIndex.end(); ++it) {
        mapReverseIndex[it->second] = it->first;
    }
}

CLibernodeMan::CLibernodeMan() : cs(),
  vLibernodes(),
  mAskedUsForLibernodeList(),
  mWeAskedForLibernodeList(),
  mWeAskedForLibernodeListEntry(),
  mWeAskedForVerification(),
  mMnbRecoveryRequests(),
  mMnbRecoveryGoodReplies(),
  listScheduledMnbRequestConnections(),
  nLastIndexRebuildTime(0),
  indexLibernodes(),
  indexLibernodesOld(),
  fIndexRebuilt(false),
  fLibernodesAdded(false),
  fLibernodesRemoved(false),
//  vecDirtyGovernanceObjectHashes(),
  nLastWatchdogVoteTime(0),
  mapSeenLibernodeBroadcast(),
  mapSeenLibernodePing(),
  nDsqCount(0)
{}

bool CLibernodeMan::Add(CLibernode &mn)
{
    LOCK(cs);

    CLibernode *pmn = Find(mn.vin);
    if (pmn == NULL) {
        LogPrint("libernode", "CLibernodeMan::Add -- Adding new Libernode: addr=%s, %i now\n", mn.addr.ToString(), size() + 1);
        vLibernodes.push_back(mn);
        indexLibernodes.AddLibernodeVIN(mn.vin);
        fLibernodesAdded = true;
        return true;
    }

    return false;
}

void CLibernodeMan::AskForMN(CNode* pnode, const CTxIn &vin)
{
    if(!pnode) return;

    LOCK(cs);

    std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it1 = mWeAskedForLibernodeListEntry.find(vin.prevout);
    if (it1 != mWeAskedForLibernodeListEntry.end()) {
        std::map<CNetAddr, int64_t>::iterator it2 = it1->second.find(pnode->addr);
        if (it2 != it1->second.end()) {
            if (GetTime() < it2->second) {
                // we've asked recently, should not repeat too often or we could get banned
                return;
            }
            // we asked this node for this outpoint but it's ok to ask again already
            LogPrintf("CLibernodeMan::AskForMN -- Asking same peer %s for missing libernode entry again: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        } else {
            // we already asked for this outpoint but not this node
            LogPrintf("CLibernodeMan::AskForMN -- Asking new peer %s for missing libernode entry: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
        }
    } else {
        // we never asked any node for this outpoint
        LogPrintf("CLibernodeMan::AskForMN -- Asking peer %s for missing libernode entry for the first time: %s\n", pnode->addr.ToString(), vin.prevout.ToStringShort());
    }
    mWeAskedForLibernodeListEntry[vin.prevout][pnode->addr] = GetTime() + DSEG_UPDATE_SECONDS;

    pnode->PushMessage(NetMsgType::DSEG, vin);
}

void CLibernodeMan::Check()
{
    LOCK(cs);

//    LogPrint("libernode", "CLibernodeMan::Check -- nLastWatchdogVoteTime=%d, IsWatchdogActive()=%d\n", nLastWatchdogVoteTime, IsWatchdogActive());

    BOOST_FOREACH(CLibernode& mn, vLibernodes) {
        mn.Check();
    }
}

void CLibernodeMan::CheckAndRemove()
{
    if(!libernodeSync.IsLibernodeListSynced()) return;

    LogPrintf("CLibernodeMan::CheckAndRemove\n");

    {
        // Need LOCK2 here to ensure consistent locking order because code below locks cs_main
        // in CheckMnbAndUpdateLibernodeList()
        LOCK2(cs_main, cs);

        Check();

        // Remove spent libernodes, prepare structures and make requests to reasure the state of inactive ones
        std::vector<CLibernode>::iterator it = vLibernodes.begin();
        std::vector<std::pair<int, CLibernode> > vecLibernodeRanks;
        // ask for up to MNB_RECOVERY_MAX_ASK_ENTRIES libernode entries at a time
        int nAskForMnbRecovery = MNB_RECOVERY_MAX_ASK_ENTRIES;
        while(it != vLibernodes.end()) {
            CLibernodeBroadcast mnb = CLibernodeBroadcast(*it);
            uint256 hash = mnb.GetHash();
            // If collateral was spent ...
            if ((*it).IsOutpointSpent()) {
                LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- Removing Libernode: %s  addr=%s  %i now\n", (*it).GetStateString(), (*it).addr.ToString(), size() - 1);

                // erase all of the broadcasts we've seen from this txin, ...
                mapSeenLibernodeBroadcast.erase(hash);
                mWeAskedForLibernodeListEntry.erase((*it).vin.prevout);

                // and finally remove it from the list
//                it->FlagGovernanceItemsAsDirty();
                it = vLibernodes.erase(it);
                fLibernodesRemoved = true;
            } else {
                bool fAsk = pCurrentBlockIndex &&
                            (nAskForMnbRecovery > 0) &&
                            libernodeSync.IsSynced() &&
                            it->IsNewStartRequired() &&
                            !IsMnbRecoveryRequested(hash);
                if(fAsk) {
                    // this mn is in a non-recoverable state and we haven't asked other nodes yet
                    std::set<CNetAddr> setRequested;
                    // calulate only once and only when it's needed
                    if(vecLibernodeRanks.empty()) {
                        int nRandomBlockHeight = GetRandInt(pCurrentBlockIndex->nHeight);
                        vecLibernodeRanks = GetLibernodeRanks(nRandomBlockHeight);
                    }
                    bool fAskedForMnbRecovery = false;
                    // ask first MNB_RECOVERY_QUORUM_TOTAL libernodes we can connect to and we haven't asked recently
                    for(int i = 0; setRequested.size() < MNB_RECOVERY_QUORUM_TOTAL && i < (int)vecLibernodeRanks.size(); i++) {
                        // avoid banning
                        if(mWeAskedForLibernodeListEntry.count(it->vin.prevout) && mWeAskedForLibernodeListEntry[it->vin.prevout].count(vecLibernodeRanks[i].second.addr)) continue;
                        // didn't ask recently, ok to ask now
                        CService addr = vecLibernodeRanks[i].second.addr;
                        setRequested.insert(addr);
                        listScheduledMnbRequestConnections.push_back(std::make_pair(addr, hash));
                        fAskedForMnbRecovery = true;
                    }
                    if(fAskedForMnbRecovery) {
                        LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- Recovery initiated, libernode=%s\n", it->vin.prevout.ToStringShort());
                        nAskForMnbRecovery--;
                    }
                    // wait for mnb recovery replies for MNB_RECOVERY_WAIT_SECONDS seconds
                    mMnbRecoveryRequests[hash] = std::make_pair(GetTime() + MNB_RECOVERY_WAIT_SECONDS, setRequested);
                }
                ++it;
            }
        }

        // proces replies for LIBERNODE_NEW_START_REQUIRED libernodes
        LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- mMnbRecoveryGoodReplies size=%d\n", (int)mMnbRecoveryGoodReplies.size());
        std::map<uint256, std::vector<CLibernodeBroadcast> >::iterator itMnbReplies = mMnbRecoveryGoodReplies.begin();
        while(itMnbReplies != mMnbRecoveryGoodReplies.end()){
            if(mMnbRecoveryRequests[itMnbReplies->first].first < GetTime()) {
                // all nodes we asked should have replied now
                if(itMnbReplies->second.size() >= MNB_RECOVERY_QUORUM_REQUIRED) {
                    // majority of nodes we asked agrees that this mn doesn't require new mnb, reprocess one of new mnbs
                    LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- reprocessing mnb, libernode=%s\n", itMnbReplies->second[0].vin.prevout.ToStringShort());
                    // mapSeenLibernodeBroadcast.erase(itMnbReplies->first);
                    int nDos;
                    itMnbReplies->second[0].fRecovery = true;
                    CheckMnbAndUpdateLibernodeList(NULL, itMnbReplies->second[0], nDos);
                }
                LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- removing mnb recovery reply, libernode=%s, size=%d\n", itMnbReplies->second[0].vin.prevout.ToStringShort(), (int)itMnbReplies->second.size());
                mMnbRecoveryGoodReplies.erase(itMnbReplies++);
            } else {
                ++itMnbReplies;
            }
        }
    }
    {
        // no need for cm_main below
        LOCK(cs);

        std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > >::iterator itMnbRequest = mMnbRecoveryRequests.begin();
        while(itMnbRequest != mMnbRecoveryRequests.end()){
            // Allow this mnb to be re-verified again after MNB_RECOVERY_RETRY_SECONDS seconds
            // if mn is still in LIBERNODE_NEW_START_REQUIRED state.
            if(GetTime() - itMnbRequest->second.first > MNB_RECOVERY_RETRY_SECONDS) {
                mMnbRecoveryRequests.erase(itMnbRequest++);
            } else {
                ++itMnbRequest;
            }
        }

        // check who's asked for the Libernode list
        std::map<CNetAddr, int64_t>::iterator it1 = mAskedUsForLibernodeList.begin();
        while(it1 != mAskedUsForLibernodeList.end()){
            if((*it1).second < GetTime()) {
                mAskedUsForLibernodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check who we asked for the Libernode list
        it1 = mWeAskedForLibernodeList.begin();
        while(it1 != mWeAskedForLibernodeList.end()){
            if((*it1).second < GetTime()){
                mWeAskedForLibernodeList.erase(it1++);
            } else {
                ++it1;
            }
        }

        // check which Libernodes we've asked for
        std::map<COutPoint, std::map<CNetAddr, int64_t> >::iterator it2 = mWeAskedForLibernodeListEntry.begin();
        while(it2 != mWeAskedForLibernodeListEntry.end()){
            std::map<CNetAddr, int64_t>::iterator it3 = it2->second.begin();
            while(it3 != it2->second.end()){
                if(it3->second < GetTime()){
                    it2->second.erase(it3++);
                } else {
                    ++it3;
                }
            }
            if(it2->second.empty()) {
                mWeAskedForLibernodeListEntry.erase(it2++);
            } else {
                ++it2;
            }
        }

        std::map<CNetAddr, CLibernodeVerification>::iterator it3 = mWeAskedForVerification.begin();
        while(it3 != mWeAskedForVerification.end()){
            if(it3->second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
                mWeAskedForVerification.erase(it3++);
            } else {
                ++it3;
            }
        }

        // NOTE: do not expire mapSeenLibernodeBroadcast entries here, clean them on mnb updates!

        // remove expired mapSeenLibernodePing
        std::map<uint256, CLibernodePing>::iterator it4 = mapSeenLibernodePing.begin();
        while(it4 != mapSeenLibernodePing.end()){
            if((*it4).second.IsExpired()) {
                LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- Removing expired Libernode ping: hash=%s\n", (*it4).second.GetHash().ToString());
                mapSeenLibernodePing.erase(it4++);
            } else {
                ++it4;
            }
        }

        // remove expired mapSeenLibernodeVerification
        std::map<uint256, CLibernodeVerification>::iterator itv2 = mapSeenLibernodeVerification.begin();
        while(itv2 != mapSeenLibernodeVerification.end()){
            if((*itv2).second.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS){
                LogPrint("libernode", "CLibernodeMan::CheckAndRemove -- Removing expired Libernode verification: hash=%s\n", (*itv2).first.ToString());
                mapSeenLibernodeVerification.erase(itv2++);
            } else {
                ++itv2;
            }
        }

        LogPrintf("CLibernodeMan::CheckAndRemove -- %s\n", ToString());

        if(fLibernodesRemoved) {
            CheckAndRebuildLibernodeIndex();
        }
    }

    if(fLibernodesRemoved) {
        NotifyLibernodeUpdates();
    }
}

void CLibernodeMan::Clear()
{
    LOCK(cs);
    vLibernodes.clear();
    mAskedUsForLibernodeList.clear();
    mWeAskedForLibernodeList.clear();
    mWeAskedForLibernodeListEntry.clear();
    mapSeenLibernodeBroadcast.clear();
    mapSeenLibernodePing.clear();
    nDsqCount = 0;
    nLastWatchdogVoteTime = 0;
    indexLibernodes.Clear();
    indexLibernodesOld.Clear();
}

int CLibernodeMan::CountLibernodes(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinLibernodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CLibernode& mn, vLibernodes) {
        if(mn.nProtocolVersion < nProtocolVersion) continue;
        nCount++;
    }

    return nCount;
}

int CLibernodeMan::CountEnabled(int nProtocolVersion)
{
    LOCK(cs);
    int nCount = 0;
    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinLibernodePaymentsProto() : nProtocolVersion;

    BOOST_FOREACH(CLibernode& mn, vLibernodes) {
        if(mn.nProtocolVersion < nProtocolVersion || !mn.IsEnabled()) continue;
        nCount++;
    }

    return nCount;
}

/* Only IPv4 libernodes are allowed in 12.1, saving this for later
int CLibernodeMan::CountByIP(int nNetworkType)
{
    LOCK(cs);
    int nNodeCount = 0;

    BOOST_FOREACH(CLibernode& mn, vLibernodes)
        if ((nNetworkType == NET_IPV4 && mn.addr.IsIPv4()) ||
            (nNetworkType == NET_TOR  && mn.addr.IsTor())  ||
            (nNetworkType == NET_IPV6 && mn.addr.IsIPv6())) {
                nNodeCount++;
        }

    return nNodeCount;
}
*/

void CLibernodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if(Params().NetworkIDString() == CBaseChainParams::MAIN) {
        if(!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForLibernodeList.find(pnode->addr);
            if(it != mWeAskedForLibernodeList.end() && GetTime() < (*it).second) {
                LogPrintf("CLibernodeMan::DsegUpdate -- we already asked %s for the list; skipping...\n", pnode->addr.ToString());
                return;
            }
        }
    }
    
    pnode->PushMessage(NetMsgType::DSEG, CTxIn());
    int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
    mWeAskedForLibernodeList[pnode->addr] = askAgain;

    LogPrint("libernode", "CLibernodeMan::DsegUpdate -- asked %s for the list\n", pnode->addr.ToString());
}

CLibernode* CLibernodeMan::Find(const CScript &payee)
{
    LOCK(cs);

    BOOST_FOREACH(CLibernode& mn, vLibernodes)
    {
        if(GetScriptForDestination(mn.pubKeyCollateralAddress.GetID()) == payee)
            return &mn;
    }
    return NULL;
}

CLibernode* CLibernodeMan::Find(const CTxIn &vin)
{
    LOCK(cs);

    BOOST_FOREACH(CLibernode& mn, vLibernodes)
    {
        if(mn.vin.prevout == vin.prevout)
            return &mn;
    }
    return NULL;
}

CLibernode* CLibernodeMan::Find(const CPubKey &pubKeyLibernode)
{
    LOCK(cs);

    BOOST_FOREACH(CLibernode& mn, vLibernodes)
    {
        if(mn.pubKeyLibernode == pubKeyLibernode)
            return &mn;
    }
    return NULL;
}

bool CLibernodeMan::Get(const CPubKey& pubKeyLibernode, CLibernode& libernode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CLibernode* pMN = Find(pubKeyLibernode);
    if(!pMN)  {
        return false;
    }
    libernode = *pMN;
    return true;
}

bool CLibernodeMan::Get(const CTxIn& vin, CLibernode& libernode)
{
    // Theses mutexes are recursive so double locking by the same thread is safe.
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN)  {
        return false;
    }
    libernode = *pMN;
    return true;
}

libernode_info_t CLibernodeMan::GetLibernodeInfo(const CTxIn& vin)
{
    libernode_info_t info;
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

libernode_info_t CLibernodeMan::GetLibernodeInfo(const CPubKey& pubKeyLibernode)
{
    libernode_info_t info;
    LOCK(cs);
    CLibernode* pMN = Find(pubKeyLibernode);
    if(!pMN)  {
        return info;
    }
    info = pMN->GetInfo();
    return info;
}

bool CLibernodeMan::Has(const CTxIn& vin)
{
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    return (pMN != NULL);
}

char* CLibernodeMan::GetNotQualifyReason(CLibernode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount)
{
    if (!mn.IsValidForPayment()) {
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'not valid for payment'");
        return reasonStr;
    }
    // //check protocol version
    if (mn.nProtocolVersion < mnpayments.GetMinLibernodePaymentsProto()) {
        // LogPrintf("Invalid nProtocolVersion!\n");
        // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
        // LogPrintf("mnpayments.GetMinLibernodePaymentsProto=%s!\n", mnpayments.GetMinLibernodePaymentsProto());
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'Invalid nProtocolVersion', nProtocolVersion=%d", mn.nProtocolVersion);
        return reasonStr;
    }
    //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
    if (mnpayments.IsScheduled(mn, nBlockHeight)) {
        // LogPrintf("mnpayments.IsScheduled!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'is scheduled'");
        return reasonStr;
    }
    //it's too new, wait for a cycle
    if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
        // LogPrintf("it's too new, wait for a cycle!\n");
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'too new', sigTime=%s, will be qualifed after=%s",
                DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
        return reasonStr;
    }
    //make sure it has at least as many confirmations as there are libernodes
    if (mn.GetCollateralAge() < nMnCount) {
        // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
        // LogPrintf("nMnCount=%s!\n", nMnCount);
        char* reasonStr = new char[256];
        sprintf(reasonStr, "false: 'collateralAge < znCount', collateralAge=%d, znCount=%d", mn.GetCollateralAge(), nMnCount);
        return reasonStr;
    }
    return NULL;
}

//
// Deterministically select the oldest/best libernode to pay on the network
//
CLibernode* CLibernodeMan::GetNextLibernodeInQueueForPayment(bool fFilterSigTime, int& nCount)
{
    if(!pCurrentBlockIndex) {
        nCount = 0;
        return NULL;
    }
    return GetNextLibernodeInQueueForPayment(pCurrentBlockIndex->nHeight, fFilterSigTime, nCount);
}

CLibernode* CLibernodeMan::GetNextLibernodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    // Need LOCK2 here to ensure consistent locking order because the GetBlockHash call below locks cs_main
    LOCK2(cs_main,cs);

    CLibernode *pBestLibernode = NULL;
    std::vector<std::pair<int, CLibernode*> > vecLibernodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */
    int nMnCount = CountEnabled();
    int index = 0;
    BOOST_FOREACH(CLibernode &mn, vLibernodes)
    {
        index += 1;
        // LogPrintf("index=%s, mn=%s\n", index, mn.ToString());
        /*if (!mn.IsValidForPayment()) {
            LogPrint("libernodeman", "Libernode, %s, addr(%s), not-qualified: 'not valid for payment'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        // //check protocol version
        if (mn.nProtocolVersion < mnpayments.GetMinLibernodePaymentsProto()) {
            // LogPrintf("Invalid nProtocolVersion!\n");
            // LogPrintf("mn.nProtocolVersion=%s!\n", mn.nProtocolVersion);
            // LogPrintf("mnpayments.GetMinLibernodePaymentsProto=%s!\n", mnpayments.GetMinLibernodePaymentsProto());
            LogPrint("libernodeman", "Libernode, %s, addr(%s), not-qualified: 'invalid nProtocolVersion'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (mnpayments.IsScheduled(mn, nBlockHeight)) {
            // LogPrintf("mnpayments.IsScheduled!\n");
            LogPrint("libernodeman", "Libernode, %s, addr(%s), not-qualified: 'IsScheduled'\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString());
            continue;
        }
        //it's too new, wait for a cycle
        if (fFilterSigTime && mn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) {
            // LogPrintf("it's too new, wait for a cycle!\n");
            LogPrint("libernodeman", "Libernode, %s, addr(%s), not-qualified: 'it's too new, wait for a cycle!', sigTime=%s, will be qualifed after=%s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime).c_str(), DateTimeStrFormat("%Y-%m-%d %H:%M UTC", mn.sigTime + (nMnCount * 2.6 * 60)).c_str());
            continue;
        }
        //make sure it has at least as many confirmations as there are libernodes
        if (mn.GetCollateralAge() < nMnCount) {
            // LogPrintf("mn.GetCollateralAge()=%s!\n", mn.GetCollateralAge());
            // LogPrintf("nMnCount=%s!\n", nMnCount);
            LogPrint("libernodeman", "Libernode, %s, addr(%s), not-qualified: 'mn.GetCollateralAge() < nMnCount', CollateralAge=%d, nMnCount=%d\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), mn.GetCollateralAge(), nMnCount);
            continue;
        }*/
        char* reasonStr = GetNotQualifyReason(mn, nBlockHeight, fFilterSigTime, nMnCount);
        if (reasonStr != NULL) {
            LogPrint("libernodeman", "Libernode, %s, addr(%s), qualify %s\n",
                     mn.vin.prevout.ToStringShort(), CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString(), reasonStr);
            delete [] reasonStr;
            continue;
        }
        vecLibernodeLastPaid.push_back(std::make_pair(mn.GetLastPaidBlock(), &mn));
    }
    nCount = (int)vecLibernodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if(fFilterSigTime && nCount < nMnCount / 3) {
        // LogPrintf("Need Return, nCount=%s, nMnCount/3=%s\n", nCount, nMnCount/3);
        return GetNextLibernodeInQueueForPayment(nBlockHeight, false, nCount);
    }

    // Sort them low to high
    sort(vecLibernodeLastPaid.begin(), vecLibernodeLastPaid.end(), CompareLastPaidBlock());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CLibernode::GetNextLibernodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }
    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    int nTenthNetwork = nMnCount/10;
    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int, CLibernode*)& s, vecLibernodeLastPaid){
        arith_uint256 nScore = s.second->CalculateScore(blockHash);
        if(nScore > nHighest){
            nHighest = nScore;
            pBestLibernode = s.second;
        }
        nCountTenth++;
        if(nCountTenth >= nTenthNetwork) break;
    }
    return pBestLibernode;
}

CLibernode* CLibernodeMan::FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion)
{
    LOCK(cs);

    nProtocolVersion = nProtocolVersion == -1 ? mnpayments.GetMinLibernodePaymentsProto() : nProtocolVersion;

    int nCountEnabled = CountEnabled(nProtocolVersion);
    int nCountNotExcluded = nCountEnabled - vecToExclude.size();

    LogPrintf("CLibernodeMan::FindRandomNotInVec -- %d enabled libernodes, %d libernodes to choose from\n", nCountEnabled, nCountNotExcluded);
    if(nCountNotExcluded < 1) return NULL;

    // fill a vector of pointers
    std::vector<CLibernode*> vpLibernodesShuffled;
    BOOST_FOREACH(CLibernode &mn, vLibernodes) {
        vpLibernodesShuffled.push_back(&mn);
    }

    InsecureRand insecureRand;
    // shuffle pointers
    std::random_shuffle(vpLibernodesShuffled.begin(), vpLibernodesShuffled.end(), insecureRand);
    bool fExclude;

    // loop through
    BOOST_FOREACH(CLibernode* pmn, vpLibernodesShuffled) {
        if(pmn->nProtocolVersion < nProtocolVersion || !pmn->IsEnabled()) continue;
        fExclude = false;
        BOOST_FOREACH(const CTxIn &txinToExclude, vecToExclude) {
            if(pmn->vin.prevout == txinToExclude.prevout) {
                fExclude = true;
                break;
            }
        }
        if(fExclude) continue;
        // found the one not in vecToExclude
        LogPrint("libernode", "CLibernodeMan::FindRandomNotInVec -- found, libernode=%s\n", pmn->vin.prevout.ToStringShort());
        return pmn;
    }

    LogPrint("libernode", "CLibernodeMan::FindRandomNotInVec -- failed\n");
    return NULL;
}

int CLibernodeMan::GetLibernodeRank(const CTxIn& vin, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CLibernode*> > vecLibernodeScores;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return -1;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CLibernode& mn, vLibernodes) {
        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive) {
            if(!mn.IsEnabled()) continue;
        }
        else {
            if(!mn.IsValidForPayment()) continue;
        }
        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecLibernodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecLibernodeScores.rbegin(), vecLibernodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CLibernode*)& scorePair, vecLibernodeScores) {
        nRank++;
        if(scorePair.second->vin.prevout == vin.prevout) return nRank;
    }

    return -1;
}

std::vector<std::pair<int, CLibernode> > CLibernodeMan::GetLibernodeRanks(int nBlockHeight, int nMinProtocol)
{
    std::vector<std::pair<int64_t, CLibernode*> > vecLibernodeScores;
    std::vector<std::pair<int, CLibernode> > vecLibernodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return vecLibernodeRanks;

    LOCK(cs);

    // scan for winner
    BOOST_FOREACH(CLibernode& mn, vLibernodes) {

        if(mn.nProtocolVersion < nMinProtocol || !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecLibernodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecLibernodeScores.rbegin(), vecLibernodeScores.rend(), CompareScoreMN());

    int nRank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CLibernode*)& s, vecLibernodeScores) {
        nRank++;
        vecLibernodeRanks.push_back(std::make_pair(nRank, *s.second));
    }

    return vecLibernodeRanks;
}

CLibernode* CLibernodeMan::GetLibernodeByRank(int nRank, int nBlockHeight, int nMinProtocol, bool fOnlyActive)
{
    std::vector<std::pair<int64_t, CLibernode*> > vecLibernodeScores;

    LOCK(cs);

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CLibernode::GetLibernodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // Fill scores
    BOOST_FOREACH(CLibernode& mn, vLibernodes) {

        if(mn.nProtocolVersion < nMinProtocol) continue;
        if(fOnlyActive && !mn.IsEnabled()) continue;

        int64_t nScore = mn.CalculateScore(blockHash).GetCompact(false);

        vecLibernodeScores.push_back(std::make_pair(nScore, &mn));
    }

    sort(vecLibernodeScores.rbegin(), vecLibernodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CLibernode*)& s, vecLibernodeScores){
        rank++;
        if(rank == nRank) {
            return s.second;
        }
    }

    return NULL;
}

void CLibernodeMan::ProcessLibernodeConnections()
{
    //we don't care about this for regtest
    if(Params().NetworkIDString() == CBaseChainParams::REGTEST) return;

    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes) {
        if(pnode->fLibernode) {
            if(darkSendPool.pSubmittedToLibernode != NULL && pnode->addr == darkSendPool.pSubmittedToLibernode->addr) continue;
            // LogPrintf("Closing Libernode connection: peer=%d, addr=%s\n", pnode->id, pnode->addr.ToString());
            pnode->fDisconnect = true;
        }
    }
}

std::pair<CService, std::set<uint256> > CLibernodeMan::PopScheduledMnbRequestConnection()
{
    LOCK(cs);
    if(listScheduledMnbRequestConnections.empty()) {
        return std::make_pair(CService(), std::set<uint256>());
    }

    std::set<uint256> setResult;

    listScheduledMnbRequestConnections.sort();
    std::pair<CService, uint256> pairFront = listScheduledMnbRequestConnections.front();

    // squash hashes from requests with the same CService as the first one into setResult
    std::list< std::pair<CService, uint256> >::iterator it = listScheduledMnbRequestConnections.begin();
    while(it != listScheduledMnbRequestConnections.end()) {
        if(pairFront.first == it->first) {
            setResult.insert(it->second);
            it = listScheduledMnbRequestConnections.erase(it);
        } else {
            // since list is sorted now, we can be sure that there is no more hashes left
            // to ask for from this addr
            break;
        }
    }
    return std::make_pair(pairFront.first, setResult);
}


void CLibernodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{

//    LogPrint("libernode", "CLibernodeMan::ProcessMessage, strCommand=%s\n", strCommand);
    if(fLiteMode) return; // disable all Dash specific functionality
    if(!libernodeSync.IsBlockchainSynced()) return;

    if (strCommand == NetMsgType::MNANNOUNCE) { //Libernode Broadcast
        CLibernodeBroadcast mnb;
        vRecv >> mnb;

        pfrom->setAskFor.erase(mnb.GetHash());

        LogPrintf("MNANNOUNCE -- Libernode announce, libernode=%s\n", mnb.vin.prevout.ToStringShort());

        int nDos = 0;

        if (CheckMnbAndUpdateLibernodeList(pfrom, mnb, nDos)) {
            // use announced Libernode as a peer
            addrman.Add(CAddress(mnb.addr, NODE_NETWORK), pfrom->addr, 2*60*60);
        } else if(nDos > 0) {
            Misbehaving(pfrom->GetId(), nDos);
        }

        if(fLibernodesAdded) {
            NotifyLibernodeUpdates();
        }
    } else if (strCommand == NetMsgType::MNPING) { //Libernode Ping

        CLibernodePing mnp;
        vRecv >> mnp;

        uint256 nHash = mnp.GetHash();

        pfrom->setAskFor.erase(nHash);

        LogPrint("libernode", "MNPING -- Libernode ping, libernode=%s\n", mnp.vin.prevout.ToStringShort());

        // Need LOCK2 here to ensure consistent locking order because the CheckAndUpdate call below locks cs_main
        LOCK2(cs_main, cs);

        if(mapSeenLibernodePing.count(nHash)) return; //seen
        mapSeenLibernodePing.insert(std::make_pair(nHash, mnp));

        LogPrint("libernode", "MNPING -- Libernode ping, libernode=%s new\n", mnp.vin.prevout.ToStringShort());

        // see if we have this Libernode
        CLibernode* pmn = mnodeman.Find(mnp.vin);

        // too late, new MNANNOUNCE is required
        if(pmn && pmn->IsNewStartRequired()) return;

        int nDos = 0;
        if(mnp.CheckAndUpdate(pmn, false, nDos)) return;

        if(nDos > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDos);
        } else if(pmn != NULL) {
            // nothing significant failed, mn is a known one too
            return;
        }

        // something significant is broken or mn is unknown,
        // we might have to ask for a libernode entry once
        AskForMN(pfrom, mnp.vin);

    } else if (strCommand == NetMsgType::DSEG) { //Get Libernode list or specific entry
        // Ignore such requests until we are fully synced.
        // We could start processing this after libernode list is synced
        // but this is a heavy one so it's better to finish sync first.
        if (!libernodeSync.IsSynced()) return;

        CTxIn vin;
        vRecv >> vin;

        LogPrint("libernode", "DSEG -- Libernode list, libernode=%s\n", vin.prevout.ToStringShort());

        LOCK(cs);

        if(vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if(!isLocal && Params().NetworkIDString() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForLibernodeList.find(pfrom->addr);
                if (i != mAskedUsForLibernodeList.end()){
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrintf("DSEG -- peer already asked me for the list, peer=%d\n", pfrom->id);
                        return;
                    }
                }
                int64_t askAgain = GetTime() + DSEG_UPDATE_SECONDS;
                mAskedUsForLibernodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok

        int nInvCount = 0;

        BOOST_FOREACH(CLibernode& mn, vLibernodes) {
            if (vin != CTxIn() && vin != mn.vin) continue; // asked for specific vin but we are not there yet
            if (mn.addr.IsRFC1918() || mn.addr.IsLocal()) continue; // do not send local network libernode
            if (mn.IsUpdateRequired()) continue; // do not send outdated libernodes

            LogPrint("libernode", "DSEG -- Sending Libernode entry: libernode=%s  addr=%s\n", mn.vin.prevout.ToStringShort(), mn.addr.ToString());
            CLibernodeBroadcast mnb = CLibernodeBroadcast(mn);
            uint256 hash = mnb.GetHash();
            pfrom->PushInventory(CInv(MSG_LIBERNODE_ANNOUNCE, hash));
            pfrom->PushInventory(CInv(MSG_LIBERNODE_PING, mn.lastPing.GetHash()));
            nInvCount++;

            if (!mapSeenLibernodeBroadcast.count(hash)) {
                mapSeenLibernodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));
            }

            if (vin == mn.vin) {
                LogPrintf("DSEG -- Sent 1 Libernode inv to peer %d\n", pfrom->id);
                return;
            }
        }

        if(vin == CTxIn()) {
            pfrom->PushMessage(NetMsgType::SYNCSTATUSCOUNT, LIBERNODE_SYNC_LIST, nInvCount);
            LogPrintf("DSEG -- Sent %d Libernode invs to peer %d\n", nInvCount, pfrom->id);
            return;
        }
        // smth weird happen - someone asked us for vin we have no idea about?
        LogPrint("libernode", "DSEG -- No invs sent to peer %d\n", pfrom->id);

    } else if (strCommand == NetMsgType::MNVERIFY) { // Libernode Verify

        // Need LOCK2 here to ensure consistent locking order because the all functions below call GetBlockHash which locks cs_main
        LOCK2(cs_main, cs);

        CLibernodeVerification mnv;
        vRecv >> mnv;

        if(mnv.vchSig1.empty()) {
            // CASE 1: someone asked me to verify myself /IP we are using/
            SendVerifyReply(pfrom, mnv);
        } else if (mnv.vchSig2.empty()) {
            // CASE 2: we _probably_ got verification we requested from some libernode
            ProcessVerifyReply(pfrom, mnv);
        } else {
            // CASE 3: we _probably_ got verification broadcast signed by some libernode which verified another one
            ProcessVerifyBroadcast(pfrom, mnv);
        }
    }
}

// Verification of libernodes via unique direct requests.

void CLibernodeMan::DoFullVerificationStep()
{
    if(activeLibernode.vin == CTxIn()) return;
    if(!libernodeSync.IsSynced()) return;

    std::vector<std::pair<int, CLibernode> > vecLibernodeRanks = GetLibernodeRanks(pCurrentBlockIndex->nHeight - 1, MIN_POSE_PROTO_VERSION);

    // Need LOCK2 here to ensure consistent locking order because the SendVerifyRequest call below locks cs_main
    // through GetHeight() signal in ConnectNode
    LOCK2(cs_main, cs);

    int nCount = 0;

    int nMyRank = -1;
    int nRanksTotal = (int)vecLibernodeRanks.size();

    // send verify requests only if we are in top MAX_POSE_RANK
    std::vector<std::pair<int, CLibernode> >::iterator it = vecLibernodeRanks.begin();
    while(it != vecLibernodeRanks.end()) {
        if(it->first > MAX_POSE_RANK) {
            LogPrint("libernode", "CLibernodeMan::DoFullVerificationStep -- Must be in top %d to send verify request\n",
                        (int)MAX_POSE_RANK);
            return;
        }
        if(it->second.vin == activeLibernode.vin) {
            nMyRank = it->first;
            LogPrint("libernode", "CLibernodeMan::DoFullVerificationStep -- Found self at rank %d/%d, verifying up to %d libernodes\n",
                        nMyRank, nRanksTotal, (int)MAX_POSE_CONNECTIONS);
            break;
        }
        ++it;
    }

    // edge case: list is too short and this libernode is not enabled
    if(nMyRank == -1) return;

    // send verify requests to up to MAX_POSE_CONNECTIONS libernodes
    // starting from MAX_POSE_RANK + nMyRank and using MAX_POSE_CONNECTIONS as a step
    int nOffset = MAX_POSE_RANK + nMyRank - 1;
    if(nOffset >= (int)vecLibernodeRanks.size()) return;

    std::vector<CLibernode*> vSortedByAddr;
    BOOST_FOREACH(CLibernode& mn, vLibernodes) {
        vSortedByAddr.push_back(&mn);
    }

    sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

    it = vecLibernodeRanks.begin() + nOffset;
    while(it != vecLibernodeRanks.end()) {
        if(it->second.IsPoSeVerified() || it->second.IsPoSeBanned()) {
            LogPrint("libernode", "CLibernodeMan::DoFullVerificationStep -- Already %s%s%s libernode %s address %s, skipping...\n",
                        it->second.IsPoSeVerified() ? "verified" : "",
                        it->second.IsPoSeVerified() && it->second.IsPoSeBanned() ? " and " : "",
                        it->second.IsPoSeBanned() ? "banned" : "",
                        it->second.vin.prevout.ToStringShort(), it->second.addr.ToString());
            nOffset += MAX_POSE_CONNECTIONS;
            if(nOffset >= (int)vecLibernodeRanks.size()) break;
            it += MAX_POSE_CONNECTIONS;
            continue;
        }
        LogPrint("libernode", "CLibernodeMan::DoFullVerificationStep -- Verifying libernode %s rank %d/%d address %s\n",
                    it->second.vin.prevout.ToStringShort(), it->first, nRanksTotal, it->second.addr.ToString());
        if(SendVerifyRequest(CAddress(it->second.addr, NODE_NETWORK), vSortedByAddr)) {
            nCount++;
            if(nCount >= MAX_POSE_CONNECTIONS) break;
        }
        nOffset += MAX_POSE_CONNECTIONS;
        if(nOffset >= (int)vecLibernodeRanks.size()) break;
        it += MAX_POSE_CONNECTIONS;
    }

    LogPrint("libernode", "CLibernodeMan::DoFullVerificationStep -- Sent verification requests to %d libernodes\n", nCount);
}

// This function tries to find libernodes with the same addr,
// find a verified one and ban all the other. If there are many nodes
// with the same addr but none of them is verified yet, then none of them are banned.
// It could take many times to run this before most of the duplicate nodes are banned.

void CLibernodeMan::CheckSameAddr()
{
    if(!libernodeSync.IsSynced() || vLibernodes.empty()) return;

    std::vector<CLibernode*> vBan;
    std::vector<CLibernode*> vSortedByAddr;

    {
        LOCK(cs);

        CLibernode* pprevLibernode = NULL;
        CLibernode* pverifiedLibernode = NULL;

        BOOST_FOREACH(CLibernode& mn, vLibernodes) {
            vSortedByAddr.push_back(&mn);
        }

        sort(vSortedByAddr.begin(), vSortedByAddr.end(), CompareByAddr());

        BOOST_FOREACH(CLibernode* pmn, vSortedByAddr) {
            // check only (pre)enabled libernodes
            if(!pmn->IsEnabled() && !pmn->IsPreEnabled()) continue;
            // initial step
            if(!pprevLibernode) {
                pprevLibernode = pmn;
                pverifiedLibernode = pmn->IsPoSeVerified() ? pmn : NULL;
                continue;
            }
            // second+ step
            if(pmn->addr == pprevLibernode->addr) {
                if(pverifiedLibernode) {
                    // another libernode with the same ip is verified, ban this one
                    vBan.push_back(pmn);
                } else if(pmn->IsPoSeVerified()) {
                    // this libernode with the same ip is verified, ban previous one
                    vBan.push_back(pprevLibernode);
                    // and keep a reference to be able to ban following libernodes with the same ip
                    pverifiedLibernode = pmn;
                }
            } else {
                pverifiedLibernode = pmn->IsPoSeVerified() ? pmn : NULL;
            }
            pprevLibernode = pmn;
        }
    }

    // ban duplicates
    BOOST_FOREACH(CLibernode* pmn, vBan) {
        LogPrintf("CLibernodeMan::CheckSameAddr -- increasing PoSe ban score for libernode %s\n", pmn->vin.prevout.ToStringShort());
        pmn->IncreasePoSeBanScore();
    }
}

bool CLibernodeMan::SendVerifyRequest(const CAddress& addr, const std::vector<CLibernode*>& vSortedByAddr)
{
    if(netfulfilledman.HasFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        // we already asked for verification, not a good idea to do this too often, skip it
        LogPrint("libernode", "CLibernodeMan::SendVerifyRequest -- too many requests, skipping... addr=%s\n", addr.ToString());
        return false;
    }

    CNode* pnode = ConnectNode(addr, NULL, false, true);
    if(pnode == NULL) {
        LogPrintf("CLibernodeMan::SendVerifyRequest -- can't connect to node to verify it, addr=%s\n", addr.ToString());
        return false;
    }

    netfulfilledman.AddFulfilledRequest(addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request");
    // use random nonce, store it and require node to reply with correct one later
    CLibernodeVerification mnv(addr, GetRandInt(999999), pCurrentBlockIndex->nHeight - 1);
    mWeAskedForVerification[addr] = mnv;
    LogPrintf("CLibernodeMan::SendVerifyRequest -- verifying node using nonce %d addr=%s\n", mnv.nonce, addr.ToString());
    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);

    return true;
}

void CLibernodeMan::SendVerifyReply(CNode* pnode, CLibernodeVerification& mnv)
{
    // only libernodes can sign this, why would someone ask regular node?
    if(!fLiberNode) {
        // do not ban, malicious node might be using my IP
        // and trying to confuse the node which tries to verify it
        return;
    }

    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply")) {
//        // peer should not ask us that often
        LogPrintf("LibernodeMan::SendVerifyReply -- ERROR: peer already asked me recently, peer=%d\n", pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        LogPrintf("LibernodeMan::SendVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    std::string strMessage = strprintf("%s%d%s", activeLibernode.service.ToString(), mnv.nonce, blockHash.ToString());

    if(!darkSendSigner.SignMessage(strMessage, mnv.vchSig1, activeLibernode.keyLibernode)) {
        LogPrintf("LibernodeMan::SendVerifyReply -- SignMessage() failed\n");
        return;
    }

    std::string strError;

    if(!darkSendSigner.VerifyMessage(activeLibernode.pubKeyLibernode, mnv.vchSig1, strMessage, strError)) {
        LogPrintf("LibernodeMan::SendVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
        return;
    }

    pnode->PushMessage(NetMsgType::MNVERIFY, mnv);
    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-reply");
}

void CLibernodeMan::ProcessVerifyReply(CNode* pnode, CLibernodeVerification& mnv)
{
    std::string strError;

    // did we even ask for it? if that's the case we should have matching fulfilled request
    if(!netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-request")) {
        LogPrintf("CLibernodeMan::ProcessVerifyReply -- ERROR: we didn't ask for verification of %s, peer=%d\n", pnode->addr.ToString(), pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nonce for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nonce != mnv.nonce) {
        LogPrintf("CLibernodeMan::ProcessVerifyReply -- ERROR: wrong nounce: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nonce, mnv.nonce, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    // Received nBlockHeight for a known address must match the one we sent
    if(mWeAskedForVerification[pnode->addr].nBlockHeight != mnv.nBlockHeight) {
        LogPrintf("CLibernodeMan::ProcessVerifyReply -- ERROR: wrong nBlockHeight: requested=%d, received=%d, peer=%d\n",
                    mWeAskedForVerification[pnode->addr].nBlockHeight, mnv.nBlockHeight, pnode->id);
        Misbehaving(pnode->id, 20);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("LibernodeMan::ProcessVerifyReply -- can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

//    // we already verified this address, why node is spamming?
    if(netfulfilledman.HasFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done")) {
        LogPrintf("CLibernodeMan::ProcessVerifyReply -- ERROR: already verified %s recently\n", pnode->addr.ToString());
        Misbehaving(pnode->id, 20);
        return;
    }

    {
        LOCK(cs);

        CLibernode* prealLibernode = NULL;
        std::vector<CLibernode*> vpLibernodesToBan;
        std::vector<CLibernode>::iterator it = vLibernodes.begin();
        std::string strMessage1 = strprintf("%s%d%s", pnode->addr.ToString(), mnv.nonce, blockHash.ToString());
        while(it != vLibernodes.end()) {
            if(CAddress(it->addr, NODE_NETWORK) == pnode->addr) {
                if(darkSendSigner.VerifyMessage(it->pubKeyLibernode, mnv.vchSig1, strMessage1, strError)) {
                    // found it!
                    prealLibernode = &(*it);
                    if(!it->IsPoSeVerified()) {
                        it->DecreasePoSeBanScore();
                    }
                    netfulfilledman.AddFulfilledRequest(pnode->addr, strprintf("%s", NetMsgType::MNVERIFY)+"-done");

                    // we can only broadcast it if we are an activated libernode
                    if(activeLibernode.vin == CTxIn()) continue;
                    // update ...
                    mnv.addr = it->addr;
                    mnv.vin1 = it->vin;
                    mnv.vin2 = activeLibernode.vin;
                    std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                            mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());
                    // ... and sign it
                    if(!darkSendSigner.SignMessage(strMessage2, mnv.vchSig2, activeLibernode.keyLibernode)) {
                        LogPrintf("LibernodeMan::ProcessVerifyReply -- SignMessage() failed\n");
                        return;
                    }

                    std::string strError;

                    if(!darkSendSigner.VerifyMessage(activeLibernode.pubKeyLibernode, mnv.vchSig2, strMessage2, strError)) {
                        LogPrintf("LibernodeMan::ProcessVerifyReply -- VerifyMessage() failed, error: %s\n", strError);
                        return;
                    }

                    mWeAskedForVerification[pnode->addr] = mnv;
                    mnv.Relay();

                } else {
                    vpLibernodesToBan.push_back(&(*it));
                }
            }
            ++it;
        }
        // no real libernode found?...
        if(!prealLibernode) {
            // this should never be the case normally,
            // only if someone is trying to game the system in some way or smth like that
            LogPrintf("CLibernodeMan::ProcessVerifyReply -- ERROR: no real libernode found for addr %s\n", pnode->addr.ToString());
            Misbehaving(pnode->id, 20);
            return;
        }
        LogPrintf("CLibernodeMan::ProcessVerifyReply -- verified real libernode %s for addr %s\n",
                    prealLibernode->vin.prevout.ToStringShort(), pnode->addr.ToString());
        // increase ban score for everyone else
        BOOST_FOREACH(CLibernode* pmn, vpLibernodesToBan) {
            pmn->IncreasePoSeBanScore();
            LogPrint("libernode", "CLibernodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        prealLibernode->vin.prevout.ToStringShort(), pnode->addr.ToString(), pmn->nPoSeBanScore);
        }
        LogPrintf("CLibernodeMan::ProcessVerifyBroadcast -- PoSe score increased for %d fake libernodes, addr %s\n",
                    (int)vpLibernodesToBan.size(), pnode->addr.ToString());
    }
}

void CLibernodeMan::ProcessVerifyBroadcast(CNode* pnode, const CLibernodeVerification& mnv)
{
    std::string strError;

    if(mapSeenLibernodeVerification.find(mnv.GetHash()) != mapSeenLibernodeVerification.end()) {
        // we already have one
        return;
    }
    mapSeenLibernodeVerification[mnv.GetHash()] = mnv;

    // we don't care about history
    if(mnv.nBlockHeight < pCurrentBlockIndex->nHeight - MAX_POSE_BLOCKS) {
        LogPrint("libernode", "LibernodeMan::ProcessVerifyBroadcast -- Outdated: current block %d, verification block %d, peer=%d\n",
                    pCurrentBlockIndex->nHeight, mnv.nBlockHeight, pnode->id);
        return;
    }

    if(mnv.vin1.prevout == mnv.vin2.prevout) {
        LogPrint("libernode", "LibernodeMan::ProcessVerifyBroadcast -- ERROR: same vins %s, peer=%d\n",
                    mnv.vin1.prevout.ToStringShort(), pnode->id);
        // that was NOT a good idea to cheat and verify itself,
        // ban the node we received such message from
        Misbehaving(pnode->id, 100);
        return;
    }

    uint256 blockHash;
    if(!GetBlockHash(blockHash, mnv.nBlockHeight)) {
        // this shouldn't happen...
        LogPrintf("LibernodeMan::ProcessVerifyBroadcast -- Can't get block hash for unknown block height %d, peer=%d\n", mnv.nBlockHeight, pnode->id);
        return;
    }

    int nRank = GetLibernodeRank(mnv.vin2, mnv.nBlockHeight, MIN_POSE_PROTO_VERSION);

    if (nRank == -1) {
        LogPrint("libernode", "CLibernodeMan::ProcessVerifyBroadcast -- Can't calculate rank for libernode %s\n",
                    mnv.vin2.prevout.ToStringShort());
        return;
    }

    if(nRank > MAX_POSE_RANK) {
        LogPrint("libernode", "CLibernodeMan::ProcessVerifyBroadcast -- Mastrernode %s is not in top %d, current rank %d, peer=%d\n",
                    mnv.vin2.prevout.ToStringShort(), (int)MAX_POSE_RANK, nRank, pnode->id);
        return;
    }

    {
        LOCK(cs);

        std::string strMessage1 = strprintf("%s%d%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString());
        std::string strMessage2 = strprintf("%s%d%s%s%s", mnv.addr.ToString(), mnv.nonce, blockHash.ToString(),
                                mnv.vin1.prevout.ToStringShort(), mnv.vin2.prevout.ToStringShort());

        CLibernode* pmn1 = Find(mnv.vin1);
        if(!pmn1) {
            LogPrintf("CLibernodeMan::ProcessVerifyBroadcast -- can't find libernode1 %s\n", mnv.vin1.prevout.ToStringShort());
            return;
        }

        CLibernode* pmn2 = Find(mnv.vin2);
        if(!pmn2) {
            LogPrintf("CLibernodeMan::ProcessVerifyBroadcast -- can't find libernode2 %s\n", mnv.vin2.prevout.ToStringShort());
            return;
        }

        if(pmn1->addr != mnv.addr) {
            LogPrintf("CLibernodeMan::ProcessVerifyBroadcast -- addr %s do not match %s\n", mnv.addr.ToString(), pnode->addr.ToString());
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn1->pubKeyLibernode, mnv.vchSig1, strMessage1, strError)) {
            LogPrintf("LibernodeMan::ProcessVerifyBroadcast -- VerifyMessage() for libernode1 failed, error: %s\n", strError);
            return;
        }

        if(darkSendSigner.VerifyMessage(pmn2->pubKeyLibernode, mnv.vchSig2, strMessage2, strError)) {
            LogPrintf("LibernodeMan::ProcessVerifyBroadcast -- VerifyMessage() for libernode2 failed, error: %s\n", strError);
            return;
        }

        if(!pmn1->IsPoSeVerified()) {
            pmn1->DecreasePoSeBanScore();
        }
        mnv.Relay();

        LogPrintf("CLibernodeMan::ProcessVerifyBroadcast -- verified libernode %s for addr %s\n",
                    pmn1->vin.prevout.ToStringShort(), pnode->addr.ToString());

        // increase ban score for everyone else with the same addr
        int nCount = 0;
        BOOST_FOREACH(CLibernode& mn, vLibernodes) {
            if(mn.addr != mnv.addr || mn.vin.prevout == mnv.vin1.prevout) continue;
            mn.IncreasePoSeBanScore();
            nCount++;
            LogPrint("libernode", "CLibernodeMan::ProcessVerifyBroadcast -- increased PoSe ban score for %s addr %s, new score %d\n",
                        mn.vin.prevout.ToStringShort(), mn.addr.ToString(), mn.nPoSeBanScore);
        }
        LogPrintf("CLibernodeMan::ProcessVerifyBroadcast -- PoSe score incresed for %d fake libernodes, addr %s\n",
                    nCount, pnode->addr.ToString());
    }
}

std::string CLibernodeMan::ToString() const
{
    std::ostringstream info;

    info << "Libernodes: " << (int)vLibernodes.size() <<
            ", peers who asked us for Libernode list: " << (int)mAskedUsForLibernodeList.size() <<
            ", peers we asked for Libernode list: " << (int)mWeAskedForLibernodeList.size() <<
            ", entries in Libernode list we asked for: " << (int)mWeAskedForLibernodeListEntry.size() <<
            ", libernode index size: " << indexLibernodes.GetSize() <<
            ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}

void CLibernodeMan::UpdateLibernodeList(CLibernodeBroadcast mnb)
{
    try {
        LogPrintf("CLibernodeMan::UpdateLibernodeList\n");
        LOCK2(cs_main, cs);
        mapSeenLibernodePing.insert(std::make_pair(mnb.lastPing.GetHash(), mnb.lastPing));
        mapSeenLibernodeBroadcast.insert(std::make_pair(mnb.GetHash(), std::make_pair(GetTime(), mnb)));

        LogPrintf("CLibernodeMan::UpdateLibernodeList -- libernode=%s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());

        CLibernode *pmn = Find(mnb.vin);
        if (pmn == NULL) {
            CLibernode mn(mnb);
            if (Add(mn)) {
                libernodeSync.AddedLibernodeList();
            }
        } else {
            CLibernodeBroadcast mnbOld = mapSeenLibernodeBroadcast[CLibernodeBroadcast(*pmn).GetHash()].second;
            if (pmn->UpdateFromNewBroadcast(mnb)) {
                libernodeSync.AddedLibernodeList();
                mapSeenLibernodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } catch (const std::exception &e) {
        PrintExceptionContinue(&e, "UpdateLibernodeList");
    }
}

bool CLibernodeMan::CheckMnbAndUpdateLibernodeList(CNode* pfrom, CLibernodeBroadcast mnb, int& nDos)
{
    // Need LOCK2 here to ensure consistent locking order because the SimpleCheck call below locks cs_main
    LOCK(cs_main);

    {
        LOCK(cs);
        nDos = 0;
        LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- libernode=%s\n", mnb.vin.prevout.ToStringShort());

        uint256 hash = mnb.GetHash();
        if (mapSeenLibernodeBroadcast.count(hash) && !mnb.fRecovery) { //seen
            LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- libernode=%s seen\n", mnb.vin.prevout.ToStringShort());
            // less then 2 pings left before this MN goes into non-recoverable state, bump sync timeout
            if (GetTime() - mapSeenLibernodeBroadcast[hash].first > LIBERNODE_NEW_START_REQUIRED_SECONDS - LIBERNODE_MIN_MNP_SECONDS * 2) {
                LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- libernode=%s seen update\n", mnb.vin.prevout.ToStringShort());
                mapSeenLibernodeBroadcast[hash].first = GetTime();
                libernodeSync.AddedLibernodeList();
            }
            // did we ask this node for it?
            if (pfrom && IsMnbRecoveryRequested(hash) && GetTime() < mMnbRecoveryRequests[hash].first) {
                LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- mnb=%s seen request\n", hash.ToString());
                if (mMnbRecoveryRequests[hash].second.count(pfrom->addr)) {
                    LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- mnb=%s seen request, addr=%s\n", hash.ToString(), pfrom->addr.ToString());
                    // do not allow node to send same mnb multiple times in recovery mode
                    mMnbRecoveryRequests[hash].second.erase(pfrom->addr);
                    // does it have newer lastPing?
                    if (mnb.lastPing.sigTime > mapSeenLibernodeBroadcast[hash].second.lastPing.sigTime) {
                        // simulate Check
                        CLibernode mnTemp = CLibernode(mnb);
                        mnTemp.Check();
                        LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- mnb=%s seen request, addr=%s, better lastPing: %d min ago, projected mn state: %s\n", hash.ToString(), pfrom->addr.ToString(), (GetTime() - mnb.lastPing.sigTime) / 60, mnTemp.GetStateString());
                        if (mnTemp.IsValidStateForAutoStart(mnTemp.nActiveState)) {
                            // this node thinks it's a good one
                            LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- libernode=%s seen good\n", mnb.vin.prevout.ToStringShort());
                            mMnbRecoveryGoodReplies[hash].push_back(mnb);
                        }
                    }
                }
            }
            return true;
        }
        mapSeenLibernodeBroadcast.insert(std::make_pair(hash, std::make_pair(GetTime(), mnb)));

        LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- libernode=%s new\n", mnb.vin.prevout.ToStringShort());

        if (!mnb.SimpleCheck(nDos)) {
            LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- SimpleCheck() failed, libernode=%s\n", mnb.vin.prevout.ToStringShort());
            return false;
        }

        // search Libernode list
        CLibernode *pmn = Find(mnb.vin);
        if (pmn) {
            CLibernodeBroadcast mnbOld = mapSeenLibernodeBroadcast[CLibernodeBroadcast(*pmn).GetHash()].second;
            if (!mnb.Update(pmn, nDos)) {
                LogPrint("libernode", "CLibernodeMan::CheckMnbAndUpdateLibernodeList -- Update() failed, libernode=%s\n", mnb.vin.prevout.ToStringShort());
                return false;
            }
            if (hash != mnbOld.GetHash()) {
                mapSeenLibernodeBroadcast.erase(mnbOld.GetHash());
            }
        }
    } // end of LOCK(cs);

    if(mnb.CheckOutpoint(nDos)) {
        Add(mnb);
        libernodeSync.AddedLibernodeList();
        // if it matches our Libernode privkey...
        if(fLiberNode && mnb.pubKeyLibernode == activeLibernode.pubKeyLibernode) {
            mnb.nPoSeBanScore = -LIBERNODE_POSE_BAN_MAX_SCORE;
            if(mnb.nProtocolVersion == PROTOCOL_VERSION) {
                // ... and PROTOCOL_VERSION, then we've been remotely activated ...
                LogPrintf("CLibernodeMan::CheckMnbAndUpdateLibernodeList -- Got NEW Libernode entry: libernode=%s  sigTime=%lld  addr=%s\n",
                            mnb.vin.prevout.ToStringShort(), mnb.sigTime, mnb.addr.ToString());
                activeLibernode.ManageState();
            } else {
                // ... otherwise we need to reactivate our node, do not add it to the list and do not relay
                // but also do not ban the node we get this message from
                LogPrintf("CLibernodeMan::CheckMnbAndUpdateLibernodeList -- wrong PROTOCOL_VERSION, re-activate your MN: message nProtocolVersion=%d  PROTOCOL_VERSION=%d\n", mnb.nProtocolVersion, PROTOCOL_VERSION);
                return false;
            }
        }
        mnb.RelayLiberNode();
    } else {
        LogPrintf("CLibernodeMan::CheckMnbAndUpdateLibernodeList -- Rejected Libernode entry: %s  addr=%s\n", mnb.vin.prevout.ToStringShort(), mnb.addr.ToString());
        return false;
    }

    return true;
}

void CLibernodeMan::UpdateLastPaid()
{
    LOCK(cs);
    if(fLiteMode) return;
    if(!pCurrentBlockIndex) {
        // LogPrintf("CLibernodeMan::UpdateLastPaid, pCurrentBlockIndex=NULL\n");
        return;
    }

    static bool IsFirstRun = true;
    // Do full scan on first run or if we are not a libernode
    // (MNs should update this info on every block, so limited scan should be enough for them)
    int nMaxBlocksToScanBack = (IsFirstRun || !fLiberNode) ? mnpayments.GetStorageLimit() : LAST_PAID_SCAN_BLOCKS;

    LogPrint("mnpayments", "CLibernodeMan::UpdateLastPaid -- nHeight=%d, nMaxBlocksToScanBack=%d, IsFirstRun=%s\n",
                             pCurrentBlockIndex->nHeight, nMaxBlocksToScanBack, IsFirstRun ? "true" : "false");

    BOOST_FOREACH(CLibernode& mn, vLibernodes) {
        mn.UpdateLastPaid(pCurrentBlockIndex, nMaxBlocksToScanBack);
    }

    // every time is like the first time if winners list is not synced
    IsFirstRun = !libernodeSync.IsWinnersListSynced();
}

void CLibernodeMan::CheckAndRebuildLibernodeIndex()
{
    LOCK(cs);

    if(GetTime() - nLastIndexRebuildTime < MIN_INDEX_REBUILD_TIME) {
        return;
    }

    if(indexLibernodes.GetSize() <= MAX_EXPECTED_INDEX_SIZE) {
        return;
    }

    if(indexLibernodes.GetSize() <= int(vLibernodes.size())) {
        return;
    }

    indexLibernodesOld = indexLibernodes;
    indexLibernodes.Clear();
    for(size_t i = 0; i < vLibernodes.size(); ++i) {
        indexLibernodes.AddLibernodeVIN(vLibernodes[i].vin);
    }

    fIndexRebuilt = true;
    nLastIndexRebuildTime = GetTime();
}

void CLibernodeMan::UpdateWatchdogVoteTime(const CTxIn& vin)
{
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->UpdateWatchdogVoteTime();
    nLastWatchdogVoteTime = GetTime();
}

bool CLibernodeMan::IsWatchdogActive()
{
    LOCK(cs);
    // Check if any libernodes have voted recently, otherwise return false
    return (GetTime() - nLastWatchdogVoteTime) <= LIBERNODE_WATCHDOG_MAX_SECONDS;
}

void CLibernodeMan::CheckLibernode(const CTxIn& vin, bool fForce)
{
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

void CLibernodeMan::CheckLibernode(const CPubKey& pubKeyLibernode, bool fForce)
{
    LOCK(cs);
    CLibernode* pMN = Find(pubKeyLibernode);
    if(!pMN)  {
        return;
    }
    pMN->Check(fForce);
}

int CLibernodeMan::GetLibernodeState(const CTxIn& vin)
{
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN)  {
        return CLibernode::LIBERNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

int CLibernodeMan::GetLibernodeState(const CPubKey& pubKeyLibernode)
{
    LOCK(cs);
    CLibernode* pMN = Find(pubKeyLibernode);
    if(!pMN)  {
        return CLibernode::LIBERNODE_NEW_START_REQUIRED;
    }
    return pMN->nActiveState;
}

bool CLibernodeMan::IsLibernodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt)
{
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN) {
        return false;
    }
    return pMN->IsPingedWithin(nSeconds, nTimeToCheckAt);
}

void CLibernodeMan::SetLibernodeLastPing(const CTxIn& vin, const CLibernodePing& mnp)
{
    LOCK(cs);
    CLibernode* pMN = Find(vin);
    if(!pMN)  {
        return;
    }
    pMN->lastPing = mnp;
    mapSeenLibernodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    CLibernodeBroadcast mnb(*pMN);
    uint256 hash = mnb.GetHash();
    if(mapSeenLibernodeBroadcast.count(hash)) {
        mapSeenLibernodeBroadcast[hash].second.lastPing = mnp;
    }
}

void CLibernodeMan::UpdatedBlockTip(const CBlockIndex *pindex)
{
    pCurrentBlockIndex = pindex;
    LogPrint("libernode", "CLibernodeMan::UpdatedBlockTip -- pCurrentBlockIndex->nHeight=%d\n", pCurrentBlockIndex->nHeight);

    CheckSameAddr();

    if(fLiberNode) {
        // normal wallet does not need to update this every block, doing update on rpc call should be enough
        UpdateLastPaid();
    }
}

void CLibernodeMan::NotifyLibernodeUpdates()
{
    // Avoid double locking
    bool fLibernodesAddedLocal = false;
    bool fLibernodesRemovedLocal = false;
    {
        LOCK(cs);
        fLibernodesAddedLocal = fLibernodesAdded;
        fLibernodesRemovedLocal = fLibernodesRemoved;
    }

    if(fLibernodesAddedLocal) {
//        governance.CheckLibernodeOrphanObjects();
//        governance.CheckLibernodeOrphanVotes();
    }
    if(fLibernodesRemovedLocal) {
//        governance.UpdateCachesAndClean();
    }

    LOCK(cs);
    fLibernodesAdded = false;
    fLibernodesRemoved = false;
}
