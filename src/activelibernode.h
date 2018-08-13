// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVELIBERNODE_H
#define ACTIVELIBERNODE_H

#include "net.h"
#include "key.h"
#include "wallet/wallet.h"

class CActiveLibernode;

static const int ACTIVE_LIBERNODE_INITIAL          = 0; // initial state
static const int ACTIVE_LIBERNODE_SYNC_IN_PROCESS  = 1;
static const int ACTIVE_LIBERNODE_INPUT_TOO_NEW    = 2;
static const int ACTIVE_LIBERNODE_NOT_CAPABLE      = 3;
static const int ACTIVE_LIBERNODE_STARTED          = 4;

extern CActiveLibernode activeLibernode;

// Responsible for activating the Libernode and pinging the network
class CActiveLibernode
{
public:
    enum libernode_type_enum_t {
        LIBERNODE_UNKNOWN = 0,
        LIBERNODE_REMOTE  = 1,
        LIBERNODE_LOCAL   = 2
    };

private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    libernode_type_enum_t eType;

    bool fPingerEnabled;

    /// Ping Libernode
    bool SendLibernodePing();

public:
    // Keys for the active Libernode
    CPubKey pubKeyLibernode;
    CKey keyLibernode;

    // Initialized while registering Libernode
    CTxIn vin;
    CService service;

    int nState; // should be one of ACTIVE_LIBERNODE_XXXX
    std::string strNotCapableReason;

    CActiveLibernode()
        : eType(LIBERNODE_UNKNOWN),
          fPingerEnabled(false),
          pubKeyLibernode(),
          keyLibernode(),
          vin(),
          service(),
          nState(ACTIVE_LIBERNODE_INITIAL)
    {}

    /// Manage state of active Libernode
    void ManageState();

    std::string GetStateString() const;
    std::string GetStatus() const;
    std::string GetTypeString() const;

private:
    void ManageStateInitial();
    void ManageStateRemote();
    void ManageStateLocal();
};

#endif
