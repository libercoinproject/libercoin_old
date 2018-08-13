#include "activelibernode.h"
#include "darksend.h"
#include "init.h"
#include "main.h"
#include "libernode-payments.h"
#include "libernode-sync.h"
#include "libernodeconfig.h"
#include "libernodeman.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "net.h"

#include <fstream>
#include <iomanip>
#include <univalue.h>

void EnsureWalletIsUnlocked();

UniValue privatesend(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() != 1)
        throw std::runtime_error(
                "privatesend \"command\"\n"
                        "\nArguments:\n"
                        "1. \"command\"        (string or set of strings, required) The command to execute\n"
                        "\nAvailable commands:\n"
                        "  start       - Start mixing\n"
                        "  stop        - Stop mixing\n"
                        "  reset       - Reset mixing\n"
        );

    if (params[0].get_str() == "start") {
        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if (fLiberNode)
            return "Mixing is not supported from libernodes";

        fEnablePrivateSend = true;
        bool result = darkSendPool.DoAutomaticDenominating();
        return "Mixing " +
               (result ? "started successfully" : ("start failed: " + darkSendPool.GetStatus() + ", will retry"));
    }

    if (params[0].get_str() == "stop") {
        fEnablePrivateSend = false;
        return "Mixing was stopped";
    }

    if (params[0].get_str() == "reset") {
        darkSendPool.ResetPool();
        return "Mixing was reset";
    }

    return "Unknown command, please see \"help privatesend\"";
}

UniValue getpoolinfo(const UniValue &params, bool fHelp) {
    if (fHelp || params.size() != 0)
        throw std::runtime_error(
                "getpoolinfo\n"
                        "Returns an object containing mixing pool related information.\n");

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("state", darkSendPool.GetStateString()));
//    obj.push_back(Pair("mixing_mode",       fPrivateSendMultiSession ? "multi-session" : "normal"));
    obj.push_back(Pair("queue", darkSendPool.GetQueueSize()));
    obj.push_back(Pair("entries", darkSendPool.GetEntriesCount()));
    obj.push_back(Pair("status", darkSendPool.GetStatus()));

    if (darkSendPool.pSubmittedToLibernode) {
        obj.push_back(Pair("outpoint", darkSendPool.pSubmittedToLibernode->vin.prevout.ToStringShort()));
        obj.push_back(Pair("addr", darkSendPool.pSubmittedToLibernode->addr.ToString()));
    }

    if (pwalletMain) {
        obj.push_back(Pair("keys_left", pwalletMain->nKeysLeftSinceAutoBackup));
        obj.push_back(Pair("warnings", pwalletMain->nKeysLeftSinceAutoBackup < PRIVATESEND_KEYS_THRESHOLD_WARNING
                                       ? "WARNING: keypool is almost depleted!" : ""));
    }

    return obj;
}


UniValue libernode(const UniValue &params, bool fHelp) {
    std::string strCommand;
    if (params.size() >= 1) {
        strCommand = params[0].get_str();
    }

    if (strCommand == "start-many")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "DEPRECATED, please use start-all instead");

    if (fHelp ||
        (strCommand != "start" && strCommand != "start-alias" && strCommand != "start-all" &&
         strCommand != "start-missing" &&
         strCommand != "start-disabled" && strCommand != "list" && strCommand != "list-conf" && strCommand != "count" &&
         strCommand != "debug" && strCommand != "current" && strCommand != "winner" && strCommand != "winners" &&
         strCommand != "genkey" &&
         strCommand != "connect" && strCommand != "outputs" && strCommand != "status"))
        throw std::runtime_error(
                "libernode \"command\"...\n"
                        "Set of commands to execute libernode related actions\n"
                        "\nArguments:\n"
                        "1. \"command\"        (string or set of strings, required) The command to execute\n"
                        "\nAvailable commands:\n"
                        "  count        - Print number of all known libernodes (optional: 'ps', 'enabled', 'all', 'qualify')\n"
                        "  current      - Print info on current libernode winner to be paid the next block (calculated locally)\n"
                        "  debug        - Print libernode status\n"
                        "  genkey       - Generate new libernodeprivkey\n"
                        "  outputs      - Print libernode compatible outputs\n"
                        "  start        - Start local Hot libernode configured in dash.conf\n"
                        "  start-alias  - Start single remote libernode by assigned alias configured in libernode.conf\n"
                        "  start-<mode> - Start remote libernodes configured in libernode.conf (<mode>: 'all', 'missing', 'disabled')\n"
                        "  status       - Print libernode status information\n"
                        "  list         - Print list of all known libernodes (see libernodelist for more info)\n"
                        "  list-conf    - Print libernode.conf in JSON format\n"
                        "  winner       - Print info on next libernode winner to vote for\n"
                        "  winners      - Print list of libernode winners\n"
        );

    if (strCommand == "list") {
        UniValue newParams(UniValue::VARR);
        // forward params but skip "list"
        for (unsigned int i = 1; i < params.size(); i++) {
            newParams.push_back(params[i]);
        }
        return libernodelist(newParams, fHelp);
    }

    if (strCommand == "connect") {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Libernode address required");

        std::string strAddress = params[1].get_str();

        CService addr = CService(strAddress);

        CNode *pnode = ConnectNode(CAddress(addr, NODE_NETWORK), NULL);
        if (!pnode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, strprintf("Couldn't connect to libernode %s", strAddress));

        return "successfully connected";
    }

    if (strCommand == "count") {
        if (params.size() > 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Too many parameters");

        if (params.size() == 1)
            return mnodeman.size();

        std::string strMode = params[1].get_str();

        if (strMode == "ps")
            return mnodeman.CountEnabled(MIN_PRIVATESEND_PEER_PROTO_VERSION);

        if (strMode == "enabled")
            return mnodeman.CountEnabled();

        int nCount;
        mnodeman.GetNextLibernodeInQueueForPayment(true, nCount);

        if (strMode == "qualify")
            return nCount;

        if (strMode == "all")
            return strprintf("Total: %d (PS Compatible: %d / Enabled: %d / Qualify: %d)",
                             mnodeman.size(), mnodeman.CountEnabled(MIN_PRIVATESEND_PEER_PROTO_VERSION),
                             mnodeman.CountEnabled(), nCount);
    }

    if (strCommand == "current" || strCommand == "winner") {
        int nCount;
        int nHeight;
        CLibernode *winner = NULL;
        {
            LOCK(cs_main);
            nHeight = chainActive.Height() + (strCommand == "current" ? 1 : 10);
        }
        mnodeman.UpdateLastPaid();
        winner = mnodeman.GetNextLibernodeInQueueForPayment(nHeight, true, nCount);
        if (!winner) return "unknown";

        UniValue obj(UniValue::VOBJ);

        obj.push_back(Pair("height", nHeight));
        obj.push_back(Pair("IP:port", winner->addr.ToString()));
        obj.push_back(Pair("protocol", (int64_t) winner->nProtocolVersion));
        obj.push_back(Pair("vin", winner->vin.prevout.ToStringShort()));
        obj.push_back(Pair("payee", CBitcoinAddress(winner->pubKeyCollateralAddress.GetID()).ToString()));
        obj.push_back(Pair("lastseen", (winner->lastPing == CLibernodePing()) ? winner->sigTime :
                                       winner->lastPing.sigTime));
        obj.push_back(Pair("activeseconds", (winner->lastPing == CLibernodePing()) ? 0 :
                                            (winner->lastPing.sigTime - winner->sigTime)));
        obj.push_back(Pair("nBlockLastPaid", winner->nBlockLastPaid));
        return obj;
    }

    if (strCommand == "debug") {
        if (activeLibernode.nState != ACTIVE_LIBERNODE_INITIAL || !libernodeSync.IsBlockchainSynced())
            return activeLibernode.GetStatus();

        CTxIn vin;
        CPubKey pubkey;
        CKey key;

        if (!pwalletMain || !pwalletMain->GetLibernodeVinAndKeys(vin, pubkey, key))
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Missing libernode input, please look at the documentation for instructions on libernode creation");

        return activeLibernode.GetStatus();
    }

    if (strCommand == "start") {
        if (!fLiberNode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "You must set libernode=1 in the configuration");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if (activeLibernode.nState != ACTIVE_LIBERNODE_STARTED) {
            activeLibernode.nState = ACTIVE_LIBERNODE_INITIAL; // TODO: consider better way
            activeLibernode.ManageState();
        }

        return activeLibernode.GetStatus();
    }

    if (strCommand == "start-alias") {
        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::string strAlias = params[1].get_str();

        bool fFound = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry mne, libernodeConfig.getEntries()) {
            if (mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CLibernodeBroadcast mnb;

                bool fResult = CLibernodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(),
                                                            mne.getOutputIndex(), strError, mnb);
                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if (fResult) {
                    mnodeman.UpdateLibernodeList(mnb);
                    mnb.RelayLiberNode();
                } else {
                    LogPrintf("Start-alias: errorMessage = %s\n", strError);
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                mnodeman.NotifyLibernodeUpdates();
                break;
            }
        }

        if (!fFound) {
            statusObj.push_back(Pair("result", "failed"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

//        LogPrintf("start-alias: statusObj=%s\n", statusObj);

        return statusObj;

    }

    if (strCommand == "start-all" || strCommand == "start-missing" || strCommand == "start-disabled") {
        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        if ((strCommand == "start-missing" || strCommand == "start-disabled") &&
            !libernodeSync.IsLibernodeListSynced()) {
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                               "You can't use this command until libernode list is synced");
        }

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);

        BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry mne, libernodeConfig.getEntries()) {
            std::string strError;

            CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CLibernode *pmn = mnodeman.Find(vin);
            CLibernodeBroadcast mnb;

            if (strCommand == "start-missing" && pmn) continue;
            if (strCommand == "start-disabled" && pmn && pmn->IsEnabled()) continue;

            bool fResult = CLibernodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(),
                                                        mne.getOutputIndex(), strError, mnb);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if (fResult) {
                nSuccessful++;
                mnodeman.UpdateLibernodeList(mnb);
                mnb.RelayLiberNode();
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }
        mnodeman.NotifyLibernodeUpdates();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall",
                                 strprintf("Successfully started %d libernodes, failed to start %d, total %d",
                                           nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "genkey") {
        CKey secret;
        secret.MakeNewKey(false);

        return CBitcoinSecret(secret).ToString();
    }

    if (strCommand == "list-conf") {
        UniValue resultObj(UniValue::VOBJ);

        BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry mne, libernodeConfig.getEntries()) {
            CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(atoi(mne.getOutputIndex().c_str())));
            CLibernode *pmn = mnodeman.Find(vin);

            std::string strStatus = pmn ? pmn->GetStatus() : "MISSING";

            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", mne.getAlias()));
            mnObj.push_back(Pair("address", mne.getIp()));
            mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
            mnObj.push_back(Pair("txHash", mne.getTxHash()));
            mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
            mnObj.push_back(Pair("status", strStatus));
            resultObj.push_back(Pair("libernode", mnObj));
        }

        return resultObj;
    }

    if (strCommand == "outputs") {
        // Find possible candidates
        std::vector <COutput> vPossibleCoins;
        pwalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, ONLY_1000);

        UniValue obj(UniValue::VOBJ);
        BOOST_FOREACH(COutput & out, vPossibleCoins)
        {
            obj.push_back(Pair(out.tx->GetHash().ToString(), strprintf("%d", out.i)));
        }

        return obj;

    }

    if (strCommand == "status") {
        if (!fLiberNode)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "This is not a libernode");

        UniValue mnObj(UniValue::VOBJ);

        mnObj.push_back(Pair("vin", activeLibernode.vin.ToString()));
        mnObj.push_back(Pair("service", activeLibernode.service.ToString()));

        CLibernode mn;
        if (mnodeman.Get(activeLibernode.vin, mn)) {
            mnObj.push_back(Pair("payee", CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString()));
        }

        mnObj.push_back(Pair("status", activeLibernode.GetStatus()));
        return mnObj;
    }

    if (strCommand == "winners") {
        int nHeight;
        {
            LOCK(cs_main);
            CBlockIndex *pindex = chainActive.Tip();
            if (!pindex) return NullUniValue;

            nHeight = pindex->nHeight;
        }

        int nLast = 10;
        std::string strFilter = "";

        if (params.size() >= 2) {
            nLast = atoi(params[1].get_str());
        }

        if (params.size() == 3) {
            strFilter = params[2].get_str();
        }

        if (params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'libernode winners ( \"count\" \"filter\" )'");

        UniValue obj(UniValue::VOBJ);

        for (int i = nHeight - nLast; i < nHeight + 20; i++) {
            std::string strPayment = GetRequiredPaymentsString(i);
            if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strprintf("%d", i), strPayment));
        }

        return obj;
    }

    return NullUniValue;
}

UniValue libernodelist(const UniValue &params, bool fHelp) {
    std::string strMode = "status";
    std::string strFilter = "";

    if (params.size() >= 1) strMode = params[0].get_str();
    if (params.size() == 2) strFilter = params[1].get_str();

    if (fHelp || (
            strMode != "activeseconds" && strMode != "addr" && strMode != "full" &&
            strMode != "lastseen" && strMode != "lastpaidtime" && strMode != "lastpaidblock" &&
            strMode != "protocol" && strMode != "payee" && strMode != "rank" && strMode != "qualify" &&
            strMode != "status")) {
        throw std::runtime_error(
                "libernodelist ( \"mode\" \"filter\" )\n"
                        "Get a list of libernodes in different modes\n"
                        "\nArguments:\n"
                        "1. \"mode\"      (string, optional/required to use filter, defaults = status) The mode to run list in\n"
                        "2. \"filter\"    (string, optional) Filter results. Partial match by outpoint by default in all modes,\n"
                        "                                    additional matches in some modes are also available\n"
                        "\nAvailable modes:\n"
                        "  activeseconds  - Print number of seconds libernode recognized by the network as enabled\n"
                        "                   (since latest issued \"libernode start/start-many/start-alias\")\n"
                        "  addr           - Print ip address associated with a libernode (can be additionally filtered, partial match)\n"
                        "  full           - Print info in format 'status protocol payee lastseen activeseconds lastpaidtime lastpaidblock IP'\n"
                        "                   (can be additionally filtered, partial match)\n"
                        "  lastpaidblock  - Print the last block height a node was paid on the network\n"
                        "  lastpaidtime   - Print the last time a node was paid on the network\n"
                        "  lastseen       - Print timestamp of when a libernode was last seen on the network\n"
                        "  payee          - Print Dash address associated with a libernode (can be additionally filtered,\n"
                        "                   partial match)\n"
                        "  protocol       - Print protocol of a libernode (can be additionally filtered, exact match))\n"
                        "  rank           - Print rank of a libernode based on current block\n"
                        "  qualify        - Print qualify status of a libernode based on current block\n"
                        "  status         - Print libernode status: PRE_ENABLED / ENABLED / EXPIRED / WATCHDOG_EXPIRED / NEW_START_REQUIRED /\n"
                        "                   UPDATE_REQUIRED / POSE_BAN / OUTPOINT_SPENT (can be additionally filtered, partial match)\n"
        );
    }

    if (strMode == "full" || strMode == "lastpaidtime" || strMode == "lastpaidblock") {
        mnodeman.UpdateLastPaid();
    }

    UniValue obj(UniValue::VOBJ);
    if (strMode == "rank") {
        std::vector <std::pair<int, CLibernode>> vLibernodeRanks = mnodeman.GetLibernodeRanks();
        BOOST_FOREACH(PAIRTYPE(int, CLibernode) & s, vLibernodeRanks)
        {
            std::string strOutpoint = s.second.vin.prevout.ToStringShort();
            if (strFilter != "" && strOutpoint.find(strFilter) == std::string::npos) continue;
            obj.push_back(Pair(strOutpoint, s.first));
        }
    } else {
        std::vector <CLibernode> vLibernodes = mnodeman.GetFullLibernodeVector();
        BOOST_FOREACH(CLibernode & mn, vLibernodes)
        {
            std::string strOutpoint = mn.vin.prevout.ToStringShort();
            if (strMode == "activeseconds") {
                if (strFilter != "" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t)(mn.lastPing.sigTime - mn.sigTime)));
            } else if (strMode == "addr") {
                std::string strAddress = mn.addr.ToString();
                if (strFilter != "" && strAddress.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos)
                    continue;
                obj.push_back(Pair(strOutpoint, strAddress));
            } else if (strMode == "full") {
                std::ostringstream streamFull;
                streamFull << std::setw(18) <<
                           mn.GetStatus() << " " <<
                           mn.nProtocolVersion << " " <<
                           CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString() << " " <<
                           (int64_t) mn.lastPing.sigTime << " " << std::setw(8) <<
                           (int64_t)(mn.lastPing.sigTime - mn.sigTime) << " " << std::setw(10) <<
                           mn.GetLastPaidTime() << " " << std::setw(6) <<
                           mn.GetLastPaidBlock() << " " <<
                           mn.addr.ToString();
                std::string strFull = streamFull.str();
                if (strFilter != "" && strFull.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos)
                    continue;
                obj.push_back(Pair(strOutpoint, strFull));
            } else if (strMode == "lastpaidblock") {
                if (strFilter != "" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, mn.GetLastPaidBlock()));
            } else if (strMode == "lastpaidtime") {
                if (strFilter != "" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, mn.GetLastPaidTime()));
            } else if (strMode == "lastseen") {
                if (strFilter != "" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (int64_t) mn.lastPing.sigTime));
            } else if (strMode == "payee") {
                CBitcoinAddress address(mn.pubKeyCollateralAddress.GetID());
                std::string strPayee = address.ToString();
                if (strFilter != "" && strPayee.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos)
                    continue;
                obj.push_back(Pair(strOutpoint, strPayee));
            } else if (strMode == "protocol") {
                if (strFilter != "" && strFilter != strprintf("%d", mn.nProtocolVersion) &&
                    strOutpoint.find(strFilter) == std::string::npos)
                    continue;
                obj.push_back(Pair(strOutpoint, (int64_t) mn.nProtocolVersion));
            } else if (strMode == "status") {
                std::string strStatus = mn.GetStatus();
                if (strFilter != "" && strStatus.find(strFilter) == std::string::npos &&
                    strOutpoint.find(strFilter) == std::string::npos)
                    continue;
                obj.push_back(Pair(strOutpoint, strStatus));
            } else if (strMode == "qualify") {
                int nBlockHeight;
                {
                    LOCK(cs_main);
                    CBlockIndex *pindex = chainActive.Tip();
                    if (!pindex) return NullUniValue;

                    nBlockHeight = pindex->nHeight;
                }
                int nMnCount = mnodeman.CountEnabled();
                char* reasonStr = mnodeman.GetNotQualifyReason(mn, nBlockHeight, true, nMnCount);
                std::string strOutpoint = mn.vin.prevout.ToStringShort();
                if (strFilter != "" && strOutpoint.find(strFilter) == std::string::npos) continue;
                obj.push_back(Pair(strOutpoint, (reasonStr != NULL) ? reasonStr : "true"));
            }
        }
    }
    return obj;
}

bool DecodeHexVecMnb(std::vector <CLibernodeBroadcast> &vecMnb, std::string strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> vecMnb;
    }
    catch (const std::exception &) {
        return false;
    }

    return true;
}

UniValue libernodebroadcast(const UniValue &params, bool fHelp) {
    std::string strCommand;
    if (params.size() >= 1)
        strCommand = params[0].get_str();

    if (fHelp ||
        (strCommand != "create-alias" && strCommand != "create-all" && strCommand != "decode" && strCommand != "relay"))
        throw std::runtime_error(
                "libernodebroadcast \"command\"...\n"
                        "Set of commands to create and relay libernode broadcast messages\n"
                        "\nArguments:\n"
                        "1. \"command\"        (string or set of strings, required) The command to execute\n"
                        "\nAvailable commands:\n"
                        "  create-alias  - Create single remote libernode broadcast message by assigned alias configured in libernode.conf\n"
                        "  create-all    - Create remote libernode broadcast messages for all libernodes configured in libernode.conf\n"
                        "  decode        - Decode libernode broadcast message\n"
                        "  relay         - Relay libernode broadcast message to the network\n"
        );

    if (strCommand == "create-alias") {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        if (params.size() < 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Please specify an alias");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        bool fFound = false;
        std::string strAlias = params[1].get_str();

        UniValue statusObj(UniValue::VOBJ);
        std::vector <CLibernodeBroadcast> vecMnb;

        statusObj.push_back(Pair("alias", strAlias));

        BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry
        mne, libernodeConfig.getEntries()) {
            if (mne.getAlias() == strAlias) {
                fFound = true;
                std::string strError;
                CLibernodeBroadcast mnb;

                bool fResult = CLibernodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(),
                                                            mne.getOutputIndex(), strError, mnb, true);

                statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));
                if (fResult) {
                    vecMnb.push_back(mnb);
                    CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
                    ssVecMnb << vecMnb;
                    statusObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));
                } else {
                    statusObj.push_back(Pair("errorMessage", strError));
                }
                break;
            }
        }

        if (!fFound) {
            statusObj.push_back(Pair("result", "not found"));
            statusObj.push_back(Pair("errorMessage", "Could not find alias in config. Verify with list-conf."));
        }

        return statusObj;

    }

    if (strCommand == "create-all") {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        {
            LOCK(pwalletMain->cs_wallet);
            EnsureWalletIsUnlocked();
        }

        std::vector <CLibernodeConfig::CLibernodeEntry> mnEntries;
        mnEntries = libernodeConfig.getEntries();

        int nSuccessful = 0;
        int nFailed = 0;

        UniValue resultsObj(UniValue::VOBJ);
        std::vector <CLibernodeBroadcast> vecMnb;

        BOOST_FOREACH(CLibernodeConfig::CLibernodeEntry
        mne, libernodeConfig.getEntries()) {
            std::string strError;
            CLibernodeBroadcast mnb;

            bool fResult = CLibernodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(),
                                                        mne.getOutputIndex(), strError, mnb, true);

            UniValue statusObj(UniValue::VOBJ);
            statusObj.push_back(Pair("alias", mne.getAlias()));
            statusObj.push_back(Pair("result", fResult ? "successful" : "failed"));

            if (fResult) {
                nSuccessful++;
                vecMnb.push_back(mnb);
            } else {
                nFailed++;
                statusObj.push_back(Pair("errorMessage", strError));
            }

            resultsObj.push_back(Pair("status", statusObj));
        }

        CDataStream ssVecMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssVecMnb << vecMnb;
        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf(
                "Successfully created broadcast messages for %d libernodes, failed to create %d, total %d",
                nSuccessful, nFailed, nSuccessful + nFailed)));
        returnObj.push_back(Pair("detail", resultsObj));
        returnObj.push_back(Pair("hex", HexStr(ssVecMnb.begin(), ssVecMnb.end())));

        return returnObj;
    }

    if (strCommand == "decode") {
        if (params.size() != 2)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Correct usage is 'libernodebroadcast decode \"hexstring\"'");

        std::vector <CLibernodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Libernode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        int nDos = 0;
        UniValue returnObj(UniValue::VOBJ);

        BOOST_FOREACH(CLibernodeBroadcast & mnb, vecMnb)
        {
            UniValue resultObj(UniValue::VOBJ);

            if (mnb.CheckSignature(nDos)) {
                nSuccessful++;
                resultObj.push_back(Pair("vin", mnb.vin.ToString()));
                resultObj.push_back(Pair("addr", mnb.addr.ToString()));
                resultObj.push_back(Pair("pubKeyCollateralAddress",
                                         CBitcoinAddress(mnb.pubKeyCollateralAddress.GetID()).ToString()));
                resultObj.push_back(Pair("pubKeyLibernode", CBitcoinAddress(mnb.pubKeyLibernode.GetID()).ToString()));
                resultObj.push_back(Pair("vchSig", EncodeBase64(&mnb.vchSig[0], mnb.vchSig.size())));
                resultObj.push_back(Pair("sigTime", mnb.sigTime));
                resultObj.push_back(Pair("protocolVersion", mnb.nProtocolVersion));
                resultObj.push_back(Pair("nLastDsq", mnb.nLastDsq));

                UniValue lastPingObj(UniValue::VOBJ);
                lastPingObj.push_back(Pair("vin", mnb.lastPing.vin.ToString()));
                lastPingObj.push_back(Pair("blockHash", mnb.lastPing.blockHash.ToString()));
                lastPingObj.push_back(Pair("sigTime", mnb.lastPing.sigTime));
                lastPingObj.push_back(
                        Pair("vchSig", EncodeBase64(&mnb.lastPing.vchSig[0], mnb.lastPing.vchSig.size())));

                resultObj.push_back(Pair("lastPing", lastPingObj));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Libernode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(mnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf(
                "Successfully decoded broadcast messages for %d libernodes, failed to decode %d, total %d",
                nSuccessful, nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    if (strCommand == "relay") {
        if (params.size() < 2 || params.size() > 3)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "libernodebroadcast relay \"hexstring\" ( fast )\n"
                    "\nArguments:\n"
                    "1. \"hex\"      (string, required) Broadcast messages hex string\n"
                    "2. fast       (string, optional) If none, using safe method\n");

        std::vector <CLibernodeBroadcast> vecMnb;

        if (!DecodeHexVecMnb(vecMnb, params[1].get_str()))
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Libernode broadcast message decode failed");

        int nSuccessful = 0;
        int nFailed = 0;
        bool fSafe = params.size() == 2;
        UniValue returnObj(UniValue::VOBJ);

        // verify all signatures first, bailout if any of them broken
        BOOST_FOREACH(CLibernodeBroadcast & mnb, vecMnb)
        {
            UniValue resultObj(UniValue::VOBJ);

            resultObj.push_back(Pair("vin", mnb.vin.ToString()));
            resultObj.push_back(Pair("addr", mnb.addr.ToString()));

            int nDos = 0;
            bool fResult;
            if (mnb.CheckSignature(nDos)) {
                if (fSafe) {
                    fResult = mnodeman.CheckMnbAndUpdateLibernodeList(NULL, mnb, nDos);
                } else {
                    mnodeman.UpdateLibernodeList(mnb);
                    mnb.RelayLiberNode();
                    fResult = true;
                }
                mnodeman.NotifyLibernodeUpdates();
            } else fResult = false;

            if (fResult) {
                nSuccessful++;
                resultObj.push_back(Pair(mnb.GetHash().ToString(), "successful"));
            } else {
                nFailed++;
                resultObj.push_back(Pair("errorMessage", "Libernode broadcast signature verification failed"));
            }

            returnObj.push_back(Pair(mnb.GetHash().ToString(), resultObj));
        }

        returnObj.push_back(Pair("overall", strprintf(
                "Successfully relayed broadcast messages for %d libernodes, failed to relay %d, total %d", nSuccessful,
                nFailed, nSuccessful + nFailed)));

        return returnObj;
    }

    return NullUniValue;
}
