// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef LIBERNODE_PAYMENTS_H
#define LIBERNODE_PAYMENTS_H

#include "util.h"
#include "core_io.h"
#include "key.h"
#include "main.h"
#include "libernode.h"
#include "utilstrencodings.h"

class CLibernodePayments;
class CLibernodePaymentVote;
class CLibernodeBlockPayees;

static const int MNPAYMENTS_SIGNATURES_REQUIRED         = 6;
static const int MNPAYMENTS_SIGNATURES_TOTAL            = 10;

//! minimum peer version that can receive and send libernode payment messages,
//  vote for libernode and be elected as a payment winner
// V1 - Last protocol version before update
// V2 - Newest protocol version
static const int MIN_LIBERNODE_PAYMENT_PROTO_VERSION_1 = 90046;
static const int MIN_LIBERNODE_PAYMENT_PROTO_VERSION_2 = 90046;

extern CCriticalSection cs_vecPayees;
extern CCriticalSection cs_mapLibernodeBlocks;
extern CCriticalSection cs_mapLibernodePayeeVotes;

extern CLibernodePayments mnpayments;

/// TODO: all 4 functions do not belong here really, they should be refactored/moved somewhere (main.cpp ?)
bool IsBlockValueValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);
bool IsBlockPayeeValid(const CTransaction& txNew, int nBlockHeight, CAmount blockReward);
void FillBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutLibernodeRet, std::vector<CTxOut>& voutSuperblockRet);
std::string GetRequiredPaymentsString(int nBlockHeight);

class CLibernodePayee
{
private:
    CScript scriptPubKey;
    std::vector<uint256> vecVoteHashes;

public:
    CLibernodePayee() :
        scriptPubKey(),
        vecVoteHashes()
        {}

    CLibernodePayee(CScript payee, uint256 hashIn) :
        scriptPubKey(payee),
        vecVoteHashes()
    {
        vecVoteHashes.push_back(hashIn);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(*(CScriptBase*)(&scriptPubKey));
        READWRITE(vecVoteHashes);
    }

    CScript GetPayee() { return scriptPubKey; }

    void AddVoteHash(uint256 hashIn) { vecVoteHashes.push_back(hashIn); }
    std::vector<uint256> GetVoteHashes() { return vecVoteHashes; }
    int GetVoteCount() { return vecVoteHashes.size(); }
    std::string ToString() const;
};

// Keep track of votes for payees from libernodes
class CLibernodeBlockPayees
{
public:
    int nBlockHeight;
    std::vector<CLibernodePayee> vecPayees;

    CLibernodeBlockPayees() :
        nBlockHeight(0),
        vecPayees()
        {}
    CLibernodeBlockPayees(int nBlockHeightIn) :
        nBlockHeight(nBlockHeightIn),
        vecPayees()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(nBlockHeight);
        READWRITE(vecPayees);
    }

    void AddPayee(const CLibernodePaymentVote& vote);
    bool GetBestPayee(CScript& payeeRet);
    bool HasPayeeWithVotes(CScript payeeIn, int nVotesReq);

    bool IsTransactionValid(const CTransaction& txNew);

    std::string GetRequiredPaymentsString();
};

// vote for the winning payment
class CLibernodePaymentVote
{
public:
    CTxIn vinLibernode;

    int nBlockHeight;
    CScript payee;
    std::vector<unsigned char> vchSig;

    CLibernodePaymentVote() :
        vinLibernode(),
        nBlockHeight(0),
        payee(),
        vchSig()
        {}

    CLibernodePaymentVote(CTxIn vinLibernode, int nBlockHeight, CScript payee) :
        vinLibernode(vinLibernode),
        nBlockHeight(nBlockHeight),
        payee(payee),
        vchSig()
        {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(vinLibernode);
        READWRITE(nBlockHeight);
        READWRITE(*(CScriptBase*)(&payee));
        READWRITE(vchSig);
    }

    uint256 GetHash() const {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << *(CScriptBase*)(&payee);
        ss << nBlockHeight;
        ss << vinLibernode.prevout;
        return ss.GetHash();
    }

    bool Sign();
    bool CheckSignature(const CPubKey& pubKeyLibernode, int nValidationHeight, int &nDos);

    bool IsValid(CNode* pnode, int nValidationHeight, std::string& strError);
    void Relay();

    bool IsVerified() { return !vchSig.empty(); }
    void MarkAsNotVerified() { vchSig.clear(); }

    std::string ToString() const;
};

//
// Libernode Payments Class
// Keeps track of who should get paid for which blocks
//

class CLibernodePayments
{
private:
    // libernode count times nStorageCoeff payments blocks should be stored ...
    const float nStorageCoeff;
    // ... but at least nMinBlocksToStore (payments blocks)
    const int nMinBlocksToStore;

    // Keep track of current block index
    const CBlockIndex *pCurrentBlockIndex;

public:
    std::map<uint256, CLibernodePaymentVote> mapLibernodePaymentVotes;
    std::map<int, CLibernodeBlockPayees> mapLibernodeBlocks;
    std::map<COutPoint, int> mapLibernodesLastVote;

    CLibernodePayments() : nStorageCoeff(1.25), nMinBlocksToStore(5000) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(mapLibernodePaymentVotes);
        READWRITE(mapLibernodeBlocks);
    }

    void Clear();

    bool AddPaymentVote(const CLibernodePaymentVote& vote);
    bool HasVerifiedPaymentVote(uint256 hashIn);
    bool ProcessBlock(int nBlockHeight);

    void Sync(CNode* node);
    void RequestLowDataPaymentBlocks(CNode* pnode);
    void CheckAndRemove();

    bool GetBlockPayee(int nBlockHeight, CScript& payee);
    bool IsTransactionValid(const CTransaction& txNew, int nBlockHeight);
    bool IsScheduled(CLibernode& mn, int nNotBlockHeight);

    bool CanVote(COutPoint outLibernode, int nBlockHeight);

    int GetMinLibernodePaymentsProto();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    std::string GetRequiredPaymentsString(int nBlockHeight);
    void FillBlockPayee(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutLibernodeRet);
    std::string ToString() const;

    int GetBlockCount() { return mapLibernodeBlocks.size(); }
    int GetVoteCount() { return mapLibernodePaymentVotes.size(); }

    bool IsEnoughData();
    int GetStorageLimit();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

#endif
