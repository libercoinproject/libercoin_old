// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef LIBERNODEMAN_H
#define LIBERNODEMAN_H

#include "libernode.h"
#include "sync.h"

using namespace std;

class CLibernodeMan;

extern CLibernodeMan mnodeman;

/**
 * Provides a forward and reverse index between MN vin's and integers.
 *
 * This mapping is normally add-only and is expected to be permanent
 * It is only rebuilt if the size of the index exceeds the expected maximum number
 * of MN's and the current number of known MN's.
 *
 * The external interface to this index is provided via delegation by CLibernodeMan
 */
class CLibernodeIndex
{
public: // Types
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

    typedef std::map<int,CTxIn> rindex_m_t;

    typedef rindex_m_t::iterator rindex_m_it;

    typedef rindex_m_t::const_iterator rindex_m_cit;

private:
    int                  nSize;

    index_m_t            mapIndex;

    rindex_m_t           mapReverseIndex;

public:
    CLibernodeIndex();

    int GetSize() const {
        return nSize;
    }

    /// Retrieve libernode vin by index
    bool Get(int nIndex, CTxIn& vinLibernode) const;

    /// Get index of a libernode vin
    int GetLibernodeIndex(const CTxIn& vinLibernode) const;

    void AddLibernodeVIN(const CTxIn& vinLibernode);

    void Clear();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(mapIndex);
        if(ser_action.ForRead()) {
            RebuildIndex();
        }
    }

private:
    void RebuildIndex();

};

class CLibernodeMan
{
public:
    typedef std::map<CTxIn,int> index_m_t;

    typedef index_m_t::iterator index_m_it;

    typedef index_m_t::const_iterator index_m_cit;

private:
    static const int MAX_EXPECTED_INDEX_SIZE = 30000;

    /// Only allow 1 index rebuild per hour
    static const int64_t MIN_INDEX_REBUILD_TIME = 3600;

    static const std::string SERIALIZATION_VERSION_STRING;

    static const int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

    static const int LAST_PAID_SCAN_BLOCKS      = 100;

    static const int MIN_POSE_PROTO_VERSION     = 70203;
    static const int MAX_POSE_CONNECTIONS       = 10;
    static const int MAX_POSE_RANK              = 10;
    static const int MAX_POSE_BLOCKS            = 10;

    static const int MNB_RECOVERY_QUORUM_TOTAL      = 10;
    static const int MNB_RECOVERY_QUORUM_REQUIRED   = 6;
    static const int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
    static const int MNB_RECOVERY_WAIT_SECONDS      = 60;
    static const int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;


    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

    // map to hold all MNs
    std::vector<CLibernode> vLibernodes;
    // who's asked for the Libernode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForLibernodeList;
    // who we asked for the Libernode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForLibernodeList;
    // which Libernodes we've asked for
    std::map<COutPoint, std::map<CNetAddr, int64_t> > mWeAskedForLibernodeListEntry;
    // who we asked for the libernode verification
    std::map<CNetAddr, CLibernodeVerification> mWeAskedForVerification;

    // these maps are used for libernode recovery from LIBERNODE_NEW_START_REQUIRED state
    std::map<uint256, std::pair< int64_t, std::set<CNetAddr> > > mMnbRecoveryRequests;
    std::map<uint256, std::vector<CLibernodeBroadcast> > mMnbRecoveryGoodReplies;
    std::list< std::pair<CService, uint256> > listScheduledMnbRequestConnections;

    int64_t nLastIndexRebuildTime;

    CLibernodeIndex indexLibernodes;

    CLibernodeIndex indexLibernodesOld;

    /// Set when index has been rebuilt, clear when read
    bool fIndexRebuilt;

    /// Set when libernodes are added, cleared when CGovernanceManager is notified
    bool fLibernodesAdded;

    /// Set when libernodes are removed, cleared when CGovernanceManager is notified
    bool fLibernodesRemoved;

    std::vector<uint256> vecDirtyGovernanceObjectHashes;

    int64_t nLastWatchdogVoteTime;

    friend class CLibernodeSync;

public:
    // Keep track of all broadcasts I've seen
    std::map<uint256, std::pair<int64_t, CLibernodeBroadcast> > mapSeenLibernodeBroadcast;
    // Keep track of all pings I've seen
    std::map<uint256, CLibernodePing> mapSeenLibernodePing;
    // Keep track of all verifications I've seen
    std::map<uint256, CLibernodeVerification> mapSeenLibernodeVerification;
    // keep track of dsq count to prevent libernodes from gaming darksend queue
    int64_t nDsqCount;


    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        std::string strVersion;
        if(ser_action.ForRead()) {
            READWRITE(strVersion);
        }
        else {
            strVersion = SERIALIZATION_VERSION_STRING; 
            READWRITE(strVersion);
        }

        READWRITE(vLibernodes);
        READWRITE(mAskedUsForLibernodeList);
        READWRITE(mWeAskedForLibernodeList);
        READWRITE(mWeAskedForLibernodeListEntry);
        READWRITE(mMnbRecoveryRequests);
        READWRITE(mMnbRecoveryGoodReplies);
        READWRITE(nLastWatchdogVoteTime);
        READWRITE(nDsqCount);

        READWRITE(mapSeenLibernodeBroadcast);
        READWRITE(mapSeenLibernodePing);
        READWRITE(indexLibernodes);
        if(ser_action.ForRead() && (strVersion != SERIALIZATION_VERSION_STRING)) {
            Clear();
        }
    }

    CLibernodeMan();

    /// Add an entry
    bool Add(CLibernode &mn);

    /// Ask (source) node for mnb
    void AskForMN(CNode *pnode, const CTxIn &vin);
    void AskForMnb(CNode *pnode, const uint256 &hash);

    /// Check all Libernodes
    void Check();

    /// Check all Libernodes and remove inactive
    void CheckAndRemove();

    /// Clear Libernode vector
    void Clear();

    /// Count Libernodes filtered by nProtocolVersion.
    /// Libernode nProtocolVersion should match or be above the one specified in param here.
    int CountLibernodes(int nProtocolVersion = -1);
    /// Count enabled Libernodes filtered by nProtocolVersion.
    /// Libernode nProtocolVersion should match or be above the one specified in param here.
    int CountEnabled(int nProtocolVersion = -1);

    /// Count Libernodes by network type - NET_IPV4, NET_IPV6, NET_TOR
    // int CountByIP(int nNetworkType);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CLibernode* Find(const CScript &payee);
    CLibernode* Find(const CTxIn& vin);
    CLibernode* Find(const CPubKey& pubKeyLibernode);

    /// Versions of Find that are safe to use from outside the class
    bool Get(const CPubKey& pubKeyLibernode, CLibernode& libernode);
    bool Get(const CTxIn& vin, CLibernode& libernode);

    /// Retrieve libernode vin by index
    bool Get(int nIndex, CTxIn& vinLibernode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexLibernodes.Get(nIndex, vinLibernode);
    }

    bool GetIndexRebuiltFlag() {
        LOCK(cs);
        return fIndexRebuilt;
    }

    /// Get index of a libernode vin
    int GetLibernodeIndex(const CTxIn& vinLibernode) {
        LOCK(cs);
        return indexLibernodes.GetLibernodeIndex(vinLibernode);
    }

    /// Get old index of a libernode vin
    int GetLibernodeIndexOld(const CTxIn& vinLibernode) {
        LOCK(cs);
        return indexLibernodesOld.GetLibernodeIndex(vinLibernode);
    }

    /// Get libernode VIN for an old index value
    bool GetLibernodeVinForIndexOld(int nLibernodeIndex, CTxIn& vinLibernodeOut) {
        LOCK(cs);
        return indexLibernodesOld.Get(nLibernodeIndex, vinLibernodeOut);
    }

    /// Get index of a libernode vin, returning rebuild flag
    int GetLibernodeIndex(const CTxIn& vinLibernode, bool& fIndexRebuiltOut) {
        LOCK(cs);
        fIndexRebuiltOut = fIndexRebuilt;
        return indexLibernodes.GetLibernodeIndex(vinLibernode);
    }

    void ClearOldLibernodeIndex() {
        LOCK(cs);
        indexLibernodesOld.Clear();
        fIndexRebuilt = false;
    }

    bool Has(const CTxIn& vin);

    libernode_info_t GetLibernodeInfo(const CTxIn& vin);

    libernode_info_t GetLibernodeInfo(const CPubKey& pubKeyLibernode);

    char* GetNotQualifyReason(CLibernode& mn, int nBlockHeight, bool fFilterSigTime, int nMnCount);

    /// Find an entry in the libernode list that is next to be paid
    CLibernode* GetNextLibernodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);
    /// Same as above but use current block height
    CLibernode* GetNextLibernodeInQueueForPayment(bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CLibernode* FindRandomNotInVec(const std::vector<CTxIn> &vecToExclude, int nProtocolVersion = -1);

    std::vector<CLibernode> GetFullLibernodeVector() { return vLibernodes; }

    std::vector<std::pair<int, CLibernode> > GetLibernodeRanks(int nBlockHeight = -1, int nMinProtocol=0);
    int GetLibernodeRank(const CTxIn &vin, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);
    CLibernode* GetLibernodeByRank(int nRank, int nBlockHeight, int nMinProtocol=0, bool fOnlyActive=true);

    void ProcessLibernodeConnections();
    std::pair<CService, std::set<uint256> > PopScheduledMnbRequestConnection();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    void DoFullVerificationStep();
    void CheckSameAddr();
    bool SendVerifyRequest(const CAddress& addr, const std::vector<CLibernode*>& vSortedByAddr);
    void SendVerifyReply(CNode* pnode, CLibernodeVerification& mnv);
    void ProcessVerifyReply(CNode* pnode, CLibernodeVerification& mnv);
    void ProcessVerifyBroadcast(CNode* pnode, const CLibernodeVerification& mnv);

    /// Return the number of (unique) Libernodes
    int size() { return vLibernodes.size(); }

    std::string ToString() const;

    /// Update libernode list and maps using provided CLibernodeBroadcast
    void UpdateLibernodeList(CLibernodeBroadcast mnb);
    /// Perform complete check and only then update list and maps
    bool CheckMnbAndUpdateLibernodeList(CNode* pfrom, CLibernodeBroadcast mnb, int& nDos);
    bool IsMnbRecoveryRequested(const uint256& hash) { return mMnbRecoveryRequests.count(hash); }

    void UpdateLastPaid();

    void CheckAndRebuildLibernodeIndex();

    void AddDirtyGovernanceObjectHash(const uint256& nHash)
    {
        LOCK(cs);
        vecDirtyGovernanceObjectHashes.push_back(nHash);
    }

    std::vector<uint256> GetAndClearDirtyGovernanceObjectHashes()
    {
        LOCK(cs);
        std::vector<uint256> vecTmp = vecDirtyGovernanceObjectHashes;
        vecDirtyGovernanceObjectHashes.clear();
        return vecTmp;;
    }

    bool IsWatchdogActive();
    void UpdateWatchdogVoteTime(const CTxIn& vin);
    bool AddGovernanceVote(const CTxIn& vin, uint256 nGovernanceObjectHash);
    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void CheckLibernode(const CTxIn& vin, bool fForce = false);
    void CheckLibernode(const CPubKey& pubKeyLibernode, bool fForce = false);

    int GetLibernodeState(const CTxIn& vin);
    int GetLibernodeState(const CPubKey& pubKeyLibernode);

    bool IsLibernodePingedWithin(const CTxIn& vin, int nSeconds, int64_t nTimeToCheckAt = -1);
    void SetLibernodeLastPing(const CTxIn& vin, const CLibernodePing& mnp);

    void UpdatedBlockTip(const CBlockIndex *pindex);

    /**
     * Called to notify CGovernanceManager that the libernode index has been updated.
     * Must be called while not holding the CLibernodeMan::cs mutex
     */
    void NotifyLibernodeUpdates();

};

#endif
