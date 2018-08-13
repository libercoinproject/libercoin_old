// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef LIBERNODE_H
#define LIBERNODE_H

#include "key.h"
#include "main.h"
#include "net.h"
#include "spork.h"
#include "timedata.h"
#include "utiltime.h"

class CLibernode;
class CLibernodeBroadcast;
class CLibernodePing;

static const int LIBERNODE_CHECK_SECONDS               =   5;
static const int LIBERNODE_MIN_MNB_SECONDS             =   5 * 60; //BROADCAST_TIME
static const int LIBERNODE_MIN_MNP_SECONDS             =  10 * 60; //PRE_ENABLE_TIME
static const int LIBERNODE_EXPIRATION_SECONDS          =  65 * 60;
static const int LIBERNODE_WATCHDOG_MAX_SECONDS        = 120 * 60;
static const int LIBERNODE_NEW_START_REQUIRED_SECONDS  = 180 * 60;
static const int LIBERNODE_COIN_REQUIRED  = 512;

static const int LIBERNODE_POSE_BAN_MAX_SCORE          = 5;
//
// The Libernode Ping Class : Contains a different serialize method for sending pings from libernodes throughout the network
//

class CLibernodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //mnb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CLibernodePing() :
        vin(),
        blockHash(),
        sigTime(0),
        vchSig()
        {}

    CLibernodePing(CTxIn& vinNew);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    void swap(CLibernodePing& first, CLibernodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    bool IsExpired() { return GetTime() - sigTime > LIBERNODE_NEW_START_REQUIRED_SECONDS; }

    bool Sign(CKey& keyLibernode, CPubKey& pubKeyLibernode);
    bool CheckSignature(CPubKey& pubKeyLibernode, int &nDos);
    bool SimpleCheck(int& nDos);
    bool CheckAndUpdate(CLibernode* pmn, bool fFromNewBroadcast, int& nDos);
    void Relay();

    CLibernodePing& operator=(CLibernodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CLibernodePing& a, const CLibernodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CLibernodePing& a, const CLibernodePing& b)
    {
        return !(a == b);
    }

};

struct libernode_info_t
{
    libernode_info_t()
        : vin(),
          addr(),
          pubKeyCollateralAddress(),
          pubKeyLibernode(),
          sigTime(0),
          nLastDsq(0),
          nTimeLastChecked(0),
          nTimeLastPaid(0),
          nTimeLastWatchdogVote(0),
          nTimeLastPing(0),
          nActiveState(0),
          nProtocolVersion(0),
          fInfoValid(false)
        {}

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyLibernode;
    int64_t sigTime; //mnb message time
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;
    int64_t nTimeLastWatchdogVote;
    int64_t nTimeLastPing;
    int nActiveState;
    int nProtocolVersion;
    bool fInfoValid;
};

//
// The Libernode Class. For managing the Darksend process. It contains the input of the 1000DRK, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CLibernode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

public:
    enum state {
        LIBERNODE_PRE_ENABLED,
        LIBERNODE_ENABLED,
        LIBERNODE_EXPIRED,
        LIBERNODE_OUTPOINT_SPENT,
        LIBERNODE_UPDATE_REQUIRED,
        LIBERNODE_WATCHDOG_EXPIRED,
        LIBERNODE_NEW_START_REQUIRED,
        LIBERNODE_POSE_BAN
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyLibernode;
    CLibernodePing lastPing;
    std::vector<unsigned char> vchSig;
    int64_t sigTime; //mnb message time
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int64_t nTimeLastChecked;
    int64_t nTimeLastPaid;
    int64_t nTimeLastWatchdogVote;
    int nActiveState;
    int nCacheCollateralBlock;
    int nBlockLastPaid;
    int nProtocolVersion;
    int nPoSeBanScore;
    int nPoSeBanHeight;
    bool fAllowMixingTx;
    bool fUnitTest;

    // KEEP TRACK OF GOVERNANCE ITEMS EACH LIBERNODE HAS VOTE UPON FOR RECALCULATION
    std::map<uint256, int> mapGovernanceObjectsVotedOn;

    CLibernode();
    CLibernode(const CLibernode& other);
    CLibernode(const CLibernodeBroadcast& mnb);
    CLibernode(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyLibernodeNew, int nProtocolVersionIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        LOCK(cs);
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyLibernode);
        READWRITE(lastPing);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nLastDsq);
        READWRITE(nTimeLastChecked);
        READWRITE(nTimeLastPaid);
        READWRITE(nTimeLastWatchdogVote);
        READWRITE(nActiveState);
        READWRITE(nCacheCollateralBlock);
        READWRITE(nBlockLastPaid);
        READWRITE(nProtocolVersion);
        READWRITE(nPoSeBanScore);
        READWRITE(nPoSeBanHeight);
        READWRITE(fAllowMixingTx);
        READWRITE(fUnitTest);
        READWRITE(mapGovernanceObjectsVotedOn);
    }

    void swap(CLibernode& first, CLibernode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyLibernode, second.pubKeyLibernode);
        swap(first.lastPing, second.lastPing);
        swap(first.vchSig, second.vchSig);
        swap(first.sigTime, second.sigTime);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nTimeLastChecked, second.nTimeLastChecked);
        swap(first.nTimeLastPaid, second.nTimeLastPaid);
        swap(first.nTimeLastWatchdogVote, second.nTimeLastWatchdogVote);
        swap(first.nActiveState, second.nActiveState);
        swap(first.nCacheCollateralBlock, second.nCacheCollateralBlock);
        swap(first.nBlockLastPaid, second.nBlockLastPaid);
        swap(first.nProtocolVersion, second.nProtocolVersion);
        swap(first.nPoSeBanScore, second.nPoSeBanScore);
        swap(first.nPoSeBanHeight, second.nPoSeBanHeight);
        swap(first.fAllowMixingTx, second.fAllowMixingTx);
        swap(first.fUnitTest, second.fUnitTest);
        swap(first.mapGovernanceObjectsVotedOn, second.mapGovernanceObjectsVotedOn);
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    bool UpdateFromNewBroadcast(CLibernodeBroadcast& mnb);

    void Check(bool fForce = false);

    bool IsBroadcastedWithin(int nSeconds) { return GetAdjustedTime() - sigTime < nSeconds; }

    bool IsPingedWithin(int nSeconds, int64_t nTimeToCheckAt = -1)
    {
        if(lastPing == CLibernodePing()) return false;

        if(nTimeToCheckAt == -1) {
            nTimeToCheckAt = GetAdjustedTime();
        }
        return nTimeToCheckAt - lastPing.sigTime < nSeconds;
    }

    bool IsEnabled() { return nActiveState == LIBERNODE_ENABLED; }
    bool IsPreEnabled() { return nActiveState == LIBERNODE_PRE_ENABLED; }
    bool IsPoSeBanned() { return nActiveState == LIBERNODE_POSE_BAN; }
    // NOTE: this one relies on nPoSeBanScore, not on nActiveState as everything else here
    bool IsPoSeVerified() { return nPoSeBanScore <= -LIBERNODE_POSE_BAN_MAX_SCORE; }
    bool IsExpired() { return nActiveState == LIBERNODE_EXPIRED; }
    bool IsOutpointSpent() { return nActiveState == LIBERNODE_OUTPOINT_SPENT; }
    bool IsUpdateRequired() { return nActiveState == LIBERNODE_UPDATE_REQUIRED; }
    bool IsWatchdogExpired() { return nActiveState == LIBERNODE_WATCHDOG_EXPIRED; }
    bool IsNewStartRequired() { return nActiveState == LIBERNODE_NEW_START_REQUIRED; }

    static bool IsValidStateForAutoStart(int nActiveStateIn)
    {
        return  nActiveStateIn == LIBERNODE_ENABLED ||
                nActiveStateIn == LIBERNODE_PRE_ENABLED ||
                nActiveStateIn == LIBERNODE_EXPIRED ||
                nActiveStateIn == LIBERNODE_WATCHDOG_EXPIRED;
    }

    bool IsValidForPayment();

    bool IsValidNetAddr();
    static bool IsValidNetAddr(CService addrIn);

    void IncreasePoSeBanScore() { if(nPoSeBanScore < LIBERNODE_POSE_BAN_MAX_SCORE) nPoSeBanScore++; }
    void DecreasePoSeBanScore() { if(nPoSeBanScore > -LIBERNODE_POSE_BAN_MAX_SCORE) nPoSeBanScore--; }

    libernode_info_t GetInfo();

    static std::string StateToString(int nStateIn);
    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string ToString() const;

    int GetCollateralAge();

    int GetLastPaidTime() { return nTimeLastPaid; }
    int GetLastPaidBlock() { return nBlockLastPaid; }
    void UpdateLastPaid(const CBlockIndex *pindex, int nMaxBlocksToScanBack);

    // KEEP TRACK OF EACH GOVERNANCE ITEM INCASE THIS NODE GOES OFFLINE, SO WE CAN RECALC THEIR STATUS
    void AddGovernanceVote(uint256 nGovernanceObjectHash);
    // RECALCULATE CACHED STATUS FLAGS FOR ALL AFFECTED OBJECTS
    void FlagGovernanceItemsAsDirty();

    void RemoveGovernanceObject(uint256 nGovernanceObjectHash);

    void UpdateWatchdogVoteTime();

    CLibernode& operator=(CLibernode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CLibernode& a, const CLibernode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CLibernode& a, const CLibernode& b)
    {
        return !(a.vin == b.vin);
    }

};


//
// The Libernode Broadcast Class : Contains a different serialize method for sending libernodes through the network
//

class CLibernodeBroadcast : public CLibernode
{
public:

    bool fRecovery;

    CLibernodeBroadcast() : CLibernode(), fRecovery(false) {}
    CLibernodeBroadcast(const CLibernode& mn) : CLibernode(mn), fRecovery(false) {}
    CLibernodeBroadcast(CService addrNew, CTxIn vinNew, CPubKey pubKeyCollateralAddressNew, CPubKey pubKeyLibernodeNew, int nProtocolVersionIn) :
        CLibernode(addrNew, vinNew, pubKeyCollateralAddressNew, pubKeyLibernodeNew, nProtocolVersionIn), fRecovery(false) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyLibernode);
        READWRITE(vchSig);
        READWRITE(sigTime);
        READWRITE(nProtocolVersion);
        READWRITE(lastPing);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << pubKeyCollateralAddress;
        ss << sigTime;
        return ss.GetHash();
    }

    /// Create Libernode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyLibernodeNew, CPubKey pubKeyLibernodeNew, std::string &strErrorRet, CLibernodeBroadcast &mnbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CLibernodeBroadcast &mnbRet, bool fOffline = false);

    bool SimpleCheck(int& nDos);
    bool Update(CLibernode* pmn, int& nDos);
    bool CheckOutpoint(int& nDos);

    bool Sign(CKey& keyCollateralAddress);
    bool CheckSignature(int& nDos);
    void RelayLiberNode();
};

class CLibernodeVerification
{
public:
    CTxIn vin1;
    CTxIn vin2;
    CService addr;
    int nonce;
    int nBlockHeight;
    std::vector<unsigned char> vchSig1;
    std::vector<unsigned char> vchSig2;

    CLibernodeVerification() :
        vin1(),
        vin2(),
        addr(),
        nonce(0),
        nBlockHeight(0),
        vchSig1(),
        vchSig2()
        {}

    CLibernodeVerification(CService addr, int nonce, int nBlockHeight) :
        vin1(),
        vin2(),
        addr(addr),
        nonce(nonce),
        nBlockHeight(nBlockHeight),
        vchSig1(),
        vchSig2()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vin1);
        READWRITE(vin2);
        READWRITE(addr);
        READWRITE(nonce);
        READWRITE(nBlockHeight);
        READWRITE(vchSig1);
        READWRITE(vchSig2);
    }

    uint256 GetHash() const
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin1;
        ss << vin2;
        ss << addr;
        ss << nonce;
        ss << nBlockHeight;
        return ss.GetHash();
    }

    void Relay() const
    {
        CInv inv(MSG_LIBERNODE_VERIFY, GetHash());
        RelayInv(inv);
    }
};

#endif
