// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "exclusivesend-server.h"

#include "activemasternode.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "txmempool.h"
#include "util.h"
#include "utilmoneystr.h"

CExclusiveSendServer privateSendServer;

void CExclusiveSendServer::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if(!fMasterNode) return;
    if(fLiteMode) return; // ignore all TriveCoin related functionality
    if(!masternodeSync.IsBlockchainSynced()) return;

    if(strCommand == NetMsgType::DSACCEPT) {

        if(pfrom->nVersion < MIN_EXCLUSIVESEND_PEER_PROTO_VERSION) {
            LogPrintf("DSACCEPT -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            PushStatus(pfrom, STATUS_REJECTED, ERR_VERSION, connman);
            return;
        }

        if(IsSessionReady()) {
            // too many users in this session already, reject new ones
            LogPrintf("DSACCEPT -- queue is already full!\n");
            PushStatus(pfrom, STATUS_ACCEPTED, ERR_QUEUE_FULL, connman);
            return;
        }

        int nDenom;
        CTransaction txCollateral;
        vRecv >> nDenom >> txCollateral;

        LogPrint("exclusivesend", "DSACCEPT -- nDenom %d (%s)  txCollateral %s", nDenom, CExclusiveSend::GetDenominationsToString(nDenom), txCollateral.ToString());

        masternode_info_t mnInfo;
        if(!mnodeman.GetMasternodeInfo(activeMasternode.outpoint, mnInfo)) {
            PushStatus(pfrom, STATUS_REJECTED, ERR_MN_LIST, connman);
            return;
        }

        if(vecSessionCollaterals.size() == 0 && mnInfo.nLastDsq != 0 &&
            mnInfo.nLastDsq + mnodeman.CountEnabled(MIN_EXCLUSIVESEND_PEER_PROTO_VERSION)/5 > mnodeman.nDsqCount)
        {
            LogPrintf("DSACCEPT -- last dsq too recent, must wait: addr=%s\n", pfrom->addr.ToString());
            PushStatus(pfrom, STATUS_REJECTED, ERR_RECENT, connman);
            return;
        }

        PoolMessage nMessageID = MSG_NOERR;

        bool fResult = nSessionID == 0  ? CreateNewSession(nDenom, txCollateral, nMessageID, connman)
                                        : AddUserToExistingSession(nDenom, txCollateral, nMessageID);
        if(fResult) {
            LogPrintf("DSACCEPT -- is compatible, please submit!\n");
            PushStatus(pfrom, STATUS_ACCEPTED, nMessageID, connman);
            return;
        } else {
            LogPrintf("DSACCEPT -- not compatible with existing transactions!\n");
            PushStatus(pfrom, STATUS_REJECTED, nMessageID, connman);
            return;
        }

    } else if(strCommand == NetMsgType::DSQUEUE) {
        TRY_LOCK(cs_darksend, lockRecv);
        if(!lockRecv) return;

        if(pfrom->nVersion < MIN_EXCLUSIVESEND_PEER_PROTO_VERSION) {
            LogPrint("exclusivesend", "DSQUEUE -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        CDarksendQueue dsq;
        vRecv >> dsq;

        // process every dsq only once
        BOOST_FOREACH(CDarksendQueue q, vecDarksendQueue) {
            if(q == dsq) {
                // LogPrint("exclusivesend", "DSQUEUE -- %s seen\n", dsq.ToString());
                return;
            }
        }

        LogPrint("exclusivesend", "DSQUEUE -- %s new\n", dsq.ToString());

        if(dsq.IsExpired()) return;

        masternode_info_t mnInfo;
        if(!mnodeman.GetMasternodeInfo(dsq.vin.prevout, mnInfo)) return;

        if(!dsq.CheckSignature(mnInfo.pubKeyMasternode)) {
            // we probably have outdated info
            mnodeman.AskForMN(pfrom, dsq.vin.prevout, connman);
            return;
        }

        if(!dsq.fReady) {
            BOOST_FOREACH(CDarksendQueue q, vecDarksendQueue) {
                if(q.vin == dsq.vin) {
                    // no way same mn can send another "not yet ready" dsq this soon
                    LogPrint("exclusivesend", "DSQUEUE -- Masternode %s is sending WAY too many dsq messages\n", mnInfo.addr.ToString());
                    return;
                }
            }

            int nThreshold = mnInfo.nLastDsq + mnodeman.CountEnabled(MIN_EXCLUSIVESEND_PEER_PROTO_VERSION)/5;
            LogPrint("exclusivesend", "DSQUEUE -- nLastDsq: %d  threshold: %d  nDsqCount: %d\n", mnInfo.nLastDsq, nThreshold, mnodeman.nDsqCount);
            //don't allow a few nodes to dominate the queuing process
            if(mnInfo.nLastDsq != 0 && nThreshold > mnodeman.nDsqCount) {
                LogPrint("exclusivesend", "DSQUEUE -- Masternode %s is sending too many dsq messages\n", mnInfo.addr.ToString());
                return;
            }
            mnodeman.AllowMixing(dsq.vin.prevout);

            LogPrint("exclusivesend", "DSQUEUE -- new ExclusiveSend queue (%s) from masternode %s\n", dsq.ToString(), mnInfo.addr.ToString());
            vecDarksendQueue.push_back(dsq);
            dsq.Relay(connman);
        }

    } else if(strCommand == NetMsgType::DSVIN) {

        if(pfrom->nVersion < MIN_EXCLUSIVESEND_PEER_PROTO_VERSION) {
            LogPrintf("DSVIN -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            PushStatus(pfrom, STATUS_REJECTED, ERR_VERSION, connman);
            return;
        }

        //do we have enough users in the current session?
        if(!IsSessionReady()) {
            LogPrintf("DSVIN -- session not complete!\n");
            PushStatus(pfrom, STATUS_REJECTED, ERR_SESSION, connman);
            return;
        }

        CDarkSendEntry entry;
        vRecv >> entry;

        LogPrint("exclusivesend", "DSVIN -- txCollateral %s", entry.txCollateral.ToString());

        if(entry.vecTxDSIn.size() > EXCLUSIVESEND_ENTRY_MAX_SIZE) {
            LogPrintf("DSVIN -- ERROR: too many inputs! %d/%d\n", entry.vecTxDSIn.size(), EXCLUSIVESEND_ENTRY_MAX_SIZE);
            PushStatus(pfrom, STATUS_REJECTED, ERR_MAXIMUM, connman);
            return;
        }

        if(entry.vecTxDSOut.size() > EXCLUSIVESEND_ENTRY_MAX_SIZE) {
            LogPrintf("DSVIN -- ERROR: too many outputs! %d/%d\n", entry.vecTxDSOut.size(), EXCLUSIVESEND_ENTRY_MAX_SIZE);
            PushStatus(pfrom, STATUS_REJECTED, ERR_MAXIMUM, connman);
            return;
        }

        //do we have the same denominations as the current session?
        if(!IsOutputsCompatibleWithSessionDenom(entry.vecTxDSOut)) {
            LogPrintf("DSVIN -- not compatible with existing transactions!\n");
            PushStatus(pfrom, STATUS_REJECTED, ERR_EXISTING_TX, connman);
            return;
        }

        //check it like a transaction
        {
            CAmount nValueIn = 0;
            CAmount nValueOut = 0;

            CMutableTransaction tx;

            BOOST_FOREACH(const CTxOut txout, entry.vecTxDSOut) {
                nValueOut += txout.nValue;
                tx.vout.push_back(txout);

                if(txout.scriptPubKey.size() != 25) {
                    LogPrintf("DSVIN -- non-standard pubkey detected! scriptPubKey=%s\n", ScriptToAsmStr(txout.scriptPubKey));
                    PushStatus(pfrom, STATUS_REJECTED, ERR_NON_STANDARD_PUBKEY, connman);
                    return;
                }
                if(!txout.scriptPubKey.IsNormalPaymentScript()) {
                    LogPrintf("DSVIN -- invalid script! scriptPubKey=%s\n", ScriptToAsmStr(txout.scriptPubKey));
                    PushStatus(pfrom, STATUS_REJECTED, ERR_INVALID_SCRIPT, connman);
                    return;
                }
            }

            BOOST_FOREACH(const CTxIn txin, entry.vecTxDSIn) {
                tx.vin.push_back(txin);

                LogPrint("exclusivesend", "DSVIN -- txin=%s\n", txin.ToString());

                CCoins coins;
                if(GetUTXOCoins(txin.prevout, coins)) {
                    nValueIn += coins.vout[txin.prevout.n].nValue;
                } else {
                    LogPrintf("DSVIN -- missing input! tx=%s", tx.ToString());
                    PushStatus(pfrom, STATUS_REJECTED, ERR_MISSING_TX, connman);
                    return;
                }
            }

            // There should be no fee in mixing tx
            CAmount nFee = nValueIn - nValueOut;
            if(nFee != 0) {
                LogPrintf("DSVIN -- there should be no fee in mixing tx! fees: %lld, tx=%s", nFee, tx.ToString());
                PushStatus(pfrom, STATUS_REJECTED, ERR_FEES, connman);
                return;
            }

            {
                LOCK(cs_main);
                CValidationState validationState;
                mempool.PrioritiseTransaction(tx.GetHash(), tx.GetHash().ToString(), 1000, 0.1*COIN);
                if(!AcceptToMemoryPool(mempool, validationState, CTransaction(tx), false, NULL, false, true, true)) {
                    LogPrintf("DSVIN -- transaction not valid! tx=%s", tx.ToString());
                    PushStatus(pfrom, STATUS_REJECTED, ERR_INVALID_TX, connman);
                    return;
                }
            }
        }

        PoolMessage nMessageID = MSG_NOERR;

        entry.addr = pfrom->addr;
        if(AddEntry(entry, nMessageID)) {
            PushStatus(pfrom, STATUS_ACCEPTED, nMessageID, connman);
            CheckPool(connman);
            RelayStatus(STATUS_ACCEPTED, connman);
        } else {
            PushStatus(pfrom, STATUS_REJECTED, nMessageID, connman);
            SetNull();
        }

    } else if(strCommand == NetMsgType::DSSIGNFINALTX) {

        if(pfrom->nVersion < MIN_EXCLUSIVESEND_PEER_PROTO_VERSION) {
            LogPrintf("DSSIGNFINALTX -- incompatible version! nVersion: %d\n", pfrom->nVersion);
            return;
        }

        std::vector<CTxIn> vecTxIn;
        vRecv >> vecTxIn;

        LogPrint("exclusivesend", "DSSIGNFINALTX -- vecTxIn.size() %s\n", vecTxIn.size());

        int nTxInIndex = 0;
        int nTxInsCount = (int)vecTxIn.size();

        BOOST_FOREACH(const CTxIn txin, vecTxIn) {
            nTxInIndex++;
            if(!AddScriptSig(txin)) {
                LogPrint("exclusivesend", "DSSIGNFINALTX -- AddScriptSig() failed at %d/%d, session: %d\n", nTxInIndex, nTxInsCount, nSessionID);
                RelayStatus(STATUS_REJECTED, connman);
                return;
            }
            LogPrint("exclusivesend", "DSSIGNFINALTX -- AddScriptSig() %d/%d success\n", nTxInIndex, nTxInsCount);
        }
        // all is good
        CheckPool(connman);
    }
}

void CExclusiveSendServer::SetNull()
{
    // MN side
    vecSessionCollaterals.clear();

    CExclusiveSendBase::SetNull();
}

//
// Check the mixing progress and send client updates if a Masternode
//
void CExclusiveSendServer::CheckPool(CConnman& connman)
{
    if(fMasterNode) {
        LogPrint("exclusivesend", "CExclusiveSendServer::CheckPool -- entries count %lu\n", GetEntriesCount());

        // If entries are full, create finalized transaction
        if(nState == POOL_STATE_ACCEPTING_ENTRIES && GetEntriesCount() >= CExclusiveSend::GetMaxPoolTransactions()) {
            LogPrint("exclusivesend", "CExclusiveSendServer::CheckPool -- FINALIZE TRANSACTIONS\n");
            CreateFinalTransaction(connman);
            return;
        }

        // If we have all of the signatures, try to compile the transaction
        if(nState == POOL_STATE_SIGNING && IsSignaturesComplete()) {
            LogPrint("exclusivesend", "CExclusiveSendServer::CheckPool -- SIGNING\n");
            CommitFinalTransaction(connman);
            return;
        }
    }

    // reset if we're here for 10 seconds
    if((nState == POOL_STATE_ERROR || nState == POOL_STATE_SUCCESS) && GetTimeMillis() - nTimeLastSuccessfulStep >= 10000) {
        LogPrint("exclusivesend", "CExclusiveSendServer::CheckPool -- timeout, RESETTING\n");
        SetNull();
    }
}

void CExclusiveSendServer::CreateFinalTransaction(CConnman& connman)
{
    LogPrint("exclusivesend", "CExclusiveSendServer::CreateFinalTransaction -- FINALIZE TRANSACTIONS\n");

    CMutableTransaction txNew;

    // make our new transaction
    for(int i = 0; i < GetEntriesCount(); i++) {
        BOOST_FOREACH(const CTxDSOut& txdsout, vecEntries[i].vecTxDSOut)
            txNew.vout.push_back(txdsout);

        BOOST_FOREACH(const CTxDSIn& txdsin, vecEntries[i].vecTxDSIn)
            txNew.vin.push_back(txdsin);
    }

    sort(txNew.vin.begin(), txNew.vin.end(), CompareInputBIP69());
    sort(txNew.vout.begin(), txNew.vout.end(), CompareOutputBIP69());

    finalMutableTransaction = txNew;
    LogPrint("exclusivesend", "CExclusiveSendServer::CreateFinalTransaction -- finalMutableTransaction=%s", txNew.ToString());

    // request signatures from clients
    RelayFinalTransaction(finalMutableTransaction, connman);
    SetState(POOL_STATE_SIGNING);
}

void CExclusiveSendServer::CommitFinalTransaction(CConnman& connman)
{
    if(!fMasterNode) return; // check and relay final tx only on masternode

    CTransaction finalTransaction = CTransaction(finalMutableTransaction);
    uint256 hashTx = finalTransaction.GetHash();

    LogPrint("exclusivesend", "CExclusiveSendServer::CommitFinalTransaction -- finalTransaction=%s", finalTransaction.ToString());

    {
        // See if the transaction is valid
        TRY_LOCK(cs_main, lockMain);
        CValidationState validationState;
        mempool.PrioritiseTransaction(hashTx, hashTx.ToString(), 1000, 0.1*COIN);
        if(!lockMain || !AcceptToMemoryPool(mempool, validationState, finalTransaction, false, NULL, false, true, true))
        {
            LogPrintf("CExclusiveSendServer::CommitFinalTransaction -- AcceptToMemoryPool() error: Transaction not valid\n");
            SetNull();
            // not much we can do in this case, just notify clients
            RelayCompletedTransaction(ERR_INVALID_TX, connman);
            return;
        }
    }

    LogPrintf("CExclusiveSendServer::CommitFinalTransaction -- CREATING DSTX\n");

    // create and sign masternode dstx transaction
    if(!CExclusiveSend::GetDSTX(hashTx)) {
        CDarksendBroadcastTx dstxNew(finalTransaction, activeMasternode.outpoint, GetAdjustedTime());
        dstxNew.Sign();
        CExclusiveSend::AddDSTX(dstxNew);
    }

    LogPrintf("CExclusiveSendServer::CommitFinalTransaction -- TRANSMITTING DSTX\n");

    CInv inv(MSG_DSTX, hashTx);
    connman.RelayInv(inv);

    // Tell the clients it was successful
    RelayCompletedTransaction(MSG_SUCCESS, connman);

    // Randomly charge clients
    ChargeRandomFees(connman);

    // Reset
    LogPrint("exclusivesend", "CExclusiveSendServer::CommitFinalTransaction -- COMPLETED -- RESETTING\n");
    SetNull();
}

//
// Charge clients a fee if they're abusive
//
// Why bother? ExclusiveSend uses collateral to ensure abuse to the process is kept to a minimum.
// The submission and signing stages are completely separate. In the cases where
// a client submits a transaction then refused to sign, there must be a cost. Otherwise they
// would be able to do this over and over again and bring the mixing to a hault.
//
// How does this work? Messages to Masternodes come in via NetMsgType::DSVIN, these require a valid collateral
// transaction for the client to be able to enter the pool. This transaction is kept by the Masternode
// until the transaction is either complete or fails.
//
void CExclusiveSendServer::ChargeFees(CConnman& connman)
{
    if(!fMasterNode) return;

    //we don't need to charge collateral for every offence.
    if(GetRandInt(100) > 33) return;

    std::vector<CTransaction> vecOffendersCollaterals;

    if(nState == POOL_STATE_ACCEPTING_ENTRIES) {
        BOOST_FOREACH(const CTransaction& txCollateral, vecSessionCollaterals) {
            bool fFound = false;
            BOOST_FOREACH(const CDarkSendEntry& entry, vecEntries)
                if(entry.txCollateral == txCollateral)
                    fFound = true;

            // This queue entry didn't send us the promised transaction
            if(!fFound) {
                LogPrintf("CExclusiveSendServer::ChargeFees -- found uncooperative node (didn't send transaction), found offence\n");
                vecOffendersCollaterals.push_back(txCollateral);
            }
        }
    }

    if(nState == POOL_STATE_SIGNING) {
        // who didn't sign?
        BOOST_FOREACH(const CDarkSendEntry entry, vecEntries) {
            BOOST_FOREACH(const CTxDSIn txdsin, entry.vecTxDSIn) {
                if(!txdsin.fHasSig) {
                    LogPrintf("CExclusiveSendServer::ChargeFees -- found uncooperative node (didn't sign), found offence\n");
                    vecOffendersCollaterals.push_back(entry.txCollateral);
                }
            }
        }
    }

    // no offences found
    if(vecOffendersCollaterals.empty()) return;

    //mostly offending? Charge sometimes
    if((int)vecOffendersCollaterals.size() >= Params().PoolMaxTransactions() - 1 && GetRandInt(100) > 33) return;

    //everyone is an offender? That's not right
    if((int)vecOffendersCollaterals.size() >= Params().PoolMaxTransactions()) return;

    //charge one of the offenders randomly
    std::random_shuffle(vecOffendersCollaterals.begin(), vecOffendersCollaterals.end());

    if(nState == POOL_STATE_ACCEPTING_ENTRIES || nState == POOL_STATE_SIGNING) {
        LogPrintf("CExclusiveSendServer::ChargeFees -- found uncooperative node (didn't %s transaction), charging fees: %s\n",
                (nState == POOL_STATE_SIGNING) ? "sign" : "send", vecOffendersCollaterals[0].ToString());

        LOCK(cs_main);

        CValidationState state;
        bool fMissingInputs;
        if(!AcceptToMemoryPool(mempool, state, vecOffendersCollaterals[0], false, &fMissingInputs, false, true)) {
            // should never really happen
            LogPrintf("CExclusiveSendServer::ChargeFees -- ERROR: AcceptToMemoryPool failed!\n");
        } else {
            connman.RelayTransaction(vecOffendersCollaterals[0]);
        }
    }
}

/*
    Charge the collateral randomly.
    Mixing is completely free, to pay miners we randomly pay the collateral of users.

    Collateral Fee Charges:

    Being that mixing has "no fees" we need to have some kind of cost associated
    with using it to stop abuse. Otherwise it could serve as an attack vector and
    allow endless transaction that would bloat TriveCoin and make it unusable. To
    stop these kinds of attacks 1 in 10 successful transactions are charged. This
    adds up to a cost of 0.001DRK per transaction on average.
*/
void CExclusiveSendServer::ChargeRandomFees(CConnman& connman)
{
    if(!fMasterNode) return;

    LOCK(cs_main);

    BOOST_FOREACH(const CTransaction& txCollateral, vecSessionCollaterals) {

        if(GetRandInt(100) > 10) return;

        LogPrintf("CExclusiveSendServer::ChargeRandomFees -- charging random fees, txCollateral=%s", txCollateral.ToString());

        CValidationState state;
        bool fMissingInputs;
        if(!AcceptToMemoryPool(mempool, state, txCollateral, false, &fMissingInputs, false, true)) {
            // should never really happen
            LogPrintf("CExclusiveSendServer::ChargeRandomFees -- ERROR: AcceptToMemoryPool failed!\n");
        } else {
            connman.RelayTransaction(txCollateral);
        }
    }
}

//
// Check for various timeouts (queue objects, mixing, etc)
//
void CExclusiveSendServer::CheckTimeout(CConnman& connman)
{
    {
        TRY_LOCK(cs_darksend, lockDS);
        if(!lockDS) return; // it's ok to fail here, we run this quite frequently

        // check mixing queue objects for timeouts
        std::vector<CDarksendQueue>::iterator it = vecDarksendQueue.begin();
        while(it != vecDarksendQueue.end()) {
            if((*it).IsExpired()) {
                LogPrint("exclusivesend", "CExclusiveSendServer::CheckTimeout -- Removing expired queue (%s)\n", (*it).ToString());
                it = vecDarksendQueue.erase(it);
            } else ++it;
        }
    }

    if(!fMasterNode) return;

    int nLagTime = fMasterNode ? 0 : 10000; // if we're the client, give the server a few extra seconds before resetting.
    int nTimeout = (nState == POOL_STATE_SIGNING) ? EXCLUSIVESEND_SIGNING_TIMEOUT : EXCLUSIVESEND_QUEUE_TIMEOUT;
    bool fTimeout = GetTimeMillis() - nTimeLastSuccessfulStep >= nTimeout*1000 + nLagTime;

    if(nState != POOL_STATE_IDLE && fTimeout) {
        LogPrint("exclusivesend", "CExclusiveSendServer::CheckTimeout -- %s timed out (%ds) -- restting\n",
                (nState == POOL_STATE_SIGNING) ? "Signing" : "Session", nTimeout);
        ChargeFees(connman);
        SetNull();
        SetState(POOL_STATE_ERROR);
    }
}

/*
    Check to see if we're ready for submissions from clients
    After receiving multiple dsa messages, the queue will switch to "accepting entries"
    which is the active state right before merging the transaction
*/
void CExclusiveSendServer::CheckForCompleteQueue(CConnman& connman)
{
    if(!fMasterNode) return;

    if(nState == POOL_STATE_QUEUE && IsSessionReady()) {
        SetState(POOL_STATE_ACCEPTING_ENTRIES);

        CDarksendQueue dsq(nSessionDenom, activeMasternode.outpoint, GetAdjustedTime(), true);
        LogPrint("exclusivesend", "CExclusiveSendServer::CheckForCompleteQueue -- queue is ready, signing and relaying (%s)\n", dsq.ToString());
        dsq.Sign();
        dsq.Relay(connman);
    }
}

// Check to make sure a given input matches an input in the pool and its scriptSig is valid
bool CExclusiveSendServer::IsInputScriptSigValid(const CTxIn& txin)
{
    CMutableTransaction txNew;
    txNew.vin.clear();
    txNew.vout.clear();

    int i = 0;
    int nTxInIndex = -1;
    CScript sigPubKey = CScript();

    BOOST_FOREACH(CDarkSendEntry& entry, vecEntries) {

        BOOST_FOREACH(const CTxDSOut& txdsout, entry.vecTxDSOut)
            txNew.vout.push_back(txdsout);

        BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn) {
            txNew.vin.push_back(txdsin);

            if(txdsin.prevout == txin.prevout) {
                nTxInIndex = i;
                sigPubKey = txdsin.prevPubKey;
            }
            i++;
        }
    }

    if(nTxInIndex >= 0) { //might have to do this one input at a time?
        txNew.vin[nTxInIndex].scriptSig = txin.scriptSig;
        LogPrint("exclusivesend", "CExclusiveSendServer::IsInputScriptSigValid -- verifying scriptSig %s\n", ScriptToAsmStr(txin.scriptSig).substr(0,24));
        if(!VerifyScript(txNew.vin[nTxInIndex].scriptSig, sigPubKey, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC, MutableTransactionSignatureChecker(&txNew, nTxInIndex))) {
            LogPrint("exclusivesend", "CExclusiveSendServer::IsInputScriptSigValid -- VerifyScript() failed on input %d\n", nTxInIndex);
            return false;
        }
    } else {
        LogPrint("exclusivesend", "CExclusiveSendServer::IsInputScriptSigValid -- Failed to find matching input in pool, %s\n", txin.ToString());
        return false;
    }

    LogPrint("exclusivesend", "CExclusiveSendServer::IsInputScriptSigValid -- Successfully validated input and scriptSig\n");
    return true;
}

//
// Add a clients transaction to the pool
//
bool CExclusiveSendServer::AddEntry(const CDarkSendEntry& entryNew, PoolMessage& nMessageIDRet)
{
    if(!fMasterNode) return false;

    BOOST_FOREACH(CTxIn txin, entryNew.vecTxDSIn) {
        if(txin.prevout.IsNull()) {
            LogPrint("exclusivesend", "CExclusiveSendServer::AddEntry -- input not valid!\n");
            nMessageIDRet = ERR_INVALID_INPUT;
            return false;
        }
    }

    if(!CExclusiveSend::IsCollateralValid(entryNew.txCollateral)) {
        LogPrint("exclusivesend", "CExclusiveSendServer::AddEntry -- collateral not valid!\n");
        nMessageIDRet = ERR_INVALID_COLLATERAL;
        return false;
    }

    if(GetEntriesCount() >= CExclusiveSend::GetMaxPoolTransactions()) {
        LogPrint("exclusivesend", "CExclusiveSendServer::AddEntry -- entries is full!\n");
        nMessageIDRet = ERR_ENTRIES_FULL;
        return false;
    }

    BOOST_FOREACH(CTxIn txin, entryNew.vecTxDSIn) {
        LogPrint("exclusivesend", "looking for txin -- %s\n", txin.ToString());
        BOOST_FOREACH(const CDarkSendEntry& entry, vecEntries) {
            BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn) {
                if(txdsin.prevout == txin.prevout) {
                    LogPrint("exclusivesend", "CExclusiveSendServer::AddEntry -- found in txin\n");
                    nMessageIDRet = ERR_ALREADY_HAVE;
                    return false;
                }
            }
        }
    }

    vecEntries.push_back(entryNew);

    LogPrint("exclusivesend", "CExclusiveSendServer::AddEntry -- adding entry\n");
    nMessageIDRet = MSG_ENTRIES_ADDED;
    nTimeLastSuccessfulStep = GetTimeMillis();

    return true;
}

bool CExclusiveSendServer::AddScriptSig(const CTxIn& txinNew)
{
    LogPrint("exclusivesend", "CExclusiveSendServer::AddScriptSig -- scriptSig=%s\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));

    BOOST_FOREACH(const CDarkSendEntry& entry, vecEntries) {
        BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn) {
            if(txdsin.scriptSig == txinNew.scriptSig) {
                LogPrint("exclusivesend", "CExclusiveSendServer::AddScriptSig -- already exists\n");
                return false;
            }
        }
    }

    if(!IsInputScriptSigValid(txinNew)) {
        LogPrint("exclusivesend", "CExclusiveSendServer::AddScriptSig -- Invalid scriptSig\n");
        return false;
    }

    LogPrint("exclusivesend", "CExclusiveSendServer::AddScriptSig -- scriptSig=%s new\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));

    BOOST_FOREACH(CTxIn& txin, finalMutableTransaction.vin) {
        if(txinNew.prevout == txin.prevout && txin.nSequence == txinNew.nSequence) {
            txin.scriptSig = txinNew.scriptSig;
            txin.prevPubKey = txinNew.prevPubKey;
            LogPrint("exclusivesend", "CExclusiveSendServer::AddScriptSig -- adding to finalMutableTransaction, scriptSig=%s\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));
        }
    }
    for(int i = 0; i < GetEntriesCount(); i++) {
        if(vecEntries[i].AddScriptSig(txinNew)) {
            LogPrint("exclusivesend", "CExclusiveSendServer::AddScriptSig -- adding to entries, scriptSig=%s\n", ScriptToAsmStr(txinNew.scriptSig).substr(0,24));
            return true;
        }
    }

    LogPrintf("CExclusiveSendServer::AddScriptSig -- Couldn't set sig!\n" );
    return false;
}

// Check to make sure everything is signed
bool CExclusiveSendServer::IsSignaturesComplete()
{
    BOOST_FOREACH(const CDarkSendEntry& entry, vecEntries)
        BOOST_FOREACH(const CTxDSIn& txdsin, entry.vecTxDSIn)
            if(!txdsin.fHasSig) return false;

    return true;
}

bool CExclusiveSendServer::IsOutputsCompatibleWithSessionDenom(const std::vector<CTxDSOut>& vecTxDSOut)
{
    if(CExclusiveSend::GetDenominations(vecTxDSOut) == 0) return false;

    BOOST_FOREACH(const CDarkSendEntry entry, vecEntries) {
        LogPrintf("CExclusiveSendServer::IsOutputsCompatibleWithSessionDenom -- vecTxDSOut denom %d, entry.vecTxDSOut denom %d\n",
                CExclusiveSend::GetDenominations(vecTxDSOut), CExclusiveSend::GetDenominations(entry.vecTxDSOut));
        if(CExclusiveSend::GetDenominations(vecTxDSOut) != CExclusiveSend::GetDenominations(entry.vecTxDSOut)) return false;
    }

    return true;
}

bool CExclusiveSendServer::IsAcceptableDenomAndCollateral(int nDenom, CTransaction txCollateral, PoolMessage& nMessageIDRet)
{
    if(!fMasterNode) return false;

    // is denom even smth legit?
    std::vector<int> vecBits;
    if(!CExclusiveSend::GetDenominationsBits(nDenom, vecBits)) {
        LogPrint("exclusivesend", "CExclusiveSendServer::IsAcceptableDenomAndCollateral -- denom not valid!\n");
        nMessageIDRet = ERR_DENOM;
        return false;
    }

    // check collateral
    if(!fUnitTest && !CExclusiveSend::IsCollateralValid(txCollateral)) {
        LogPrint("exclusivesend", "CExclusiveSendServer::IsAcceptableDenomAndCollateral -- collateral not valid!\n");
        nMessageIDRet = ERR_INVALID_COLLATERAL;
        return false;
    }

    return true;
}

bool CExclusiveSendServer::CreateNewSession(int nDenom, CTransaction txCollateral, PoolMessage& nMessageIDRet, CConnman& connman)
{
    if(!fMasterNode || nSessionID != 0) return false;

    // new session can only be started in idle mode
    if(nState != POOL_STATE_IDLE) {
        nMessageIDRet = ERR_MODE;
        LogPrintf("CExclusiveSendServer::CreateNewSession -- incompatible mode: nState=%d\n", nState);
        return false;
    }

    if(!IsAcceptableDenomAndCollateral(nDenom, txCollateral, nMessageIDRet)) {
        return false;
    }

    // start new session
    nMessageIDRet = MSG_NOERR;
    nSessionID = GetRandInt(999999)+1;
    nSessionDenom = nDenom;

    SetState(POOL_STATE_QUEUE);
    nTimeLastSuccessfulStep = GetTimeMillis();

    if(!fUnitTest) {
        //broadcast that I'm accepting entries, only if it's the first entry through
        CDarksendQueue dsq(nDenom, activeMasternode.outpoint, GetAdjustedTime(), false);
        LogPrint("exclusivesend", "CExclusiveSendServer::CreateNewSession -- signing and relaying new queue: %s\n", dsq.ToString());
        dsq.Sign();
        dsq.Relay(connman);
        vecDarksendQueue.push_back(dsq);
    }

    vecSessionCollaterals.push_back(txCollateral);
    LogPrintf("CExclusiveSendServer::CreateNewSession -- new session created, nSessionID: %d  nSessionDenom: %d (%s)  vecSessionCollaterals.size(): %d\n",
            nSessionID, nSessionDenom, CExclusiveSend::GetDenominationsToString(nSessionDenom), vecSessionCollaterals.size());

    return true;
}

bool CExclusiveSendServer::AddUserToExistingSession(int nDenom, CTransaction txCollateral, PoolMessage& nMessageIDRet)
{
    if(!fMasterNode || nSessionID == 0 || IsSessionReady()) return false;

    if(!IsAcceptableDenomAndCollateral(nDenom, txCollateral, nMessageIDRet)) {
        return false;
    }

    // we only add new users to an existing session when we are in queue mode
    if(nState != POOL_STATE_QUEUE) {
        nMessageIDRet = ERR_MODE;
        LogPrintf("CExclusiveSendServer::AddUserToExistingSession -- incompatible mode: nState=%d\n", nState);
        return false;
    }

    if(nDenom != nSessionDenom) {
        LogPrintf("CExclusiveSendServer::AddUserToExistingSession -- incompatible denom %d (%s) != nSessionDenom %d (%s)\n",
                    nDenom, CExclusiveSend::GetDenominationsToString(nDenom), nSessionDenom, CExclusiveSend::GetDenominationsToString(nSessionDenom));
        nMessageIDRet = ERR_DENOM;
        return false;
    }

    // count new user as accepted to an existing session

    nMessageIDRet = MSG_NOERR;
    nTimeLastSuccessfulStep = GetTimeMillis();
    vecSessionCollaterals.push_back(txCollateral);

    LogPrintf("CExclusiveSendServer::AddUserToExistingSession -- new user accepted, nSessionID: %d  nSessionDenom: %d (%s)  vecSessionCollaterals.size(): %d\n",
            nSessionID, nSessionDenom, CExclusiveSend::GetDenominationsToString(nSessionDenom), vecSessionCollaterals.size());

    return true;
}

void CExclusiveSendServer::RelayFinalTransaction(const CTransaction& txFinal, CConnman& connman)
{
    LogPrint("exclusivesend", "CExclusiveSendServer::%s -- nSessionID: %d  nSessionDenom: %d (%s)\n",
            __func__, nSessionID, nSessionDenom, CExclusiveSend::GetDenominationsToString(nSessionDenom));

    // final mixing tx with empty signatures should be relayed to mixing participants only
    for (const auto entry : vecEntries) {
        bool fOk = connman.ForNode(entry.addr, [&txFinal, &connman, this](CNode* pnode) {
            connman.PushMessage(pnode, NetMsgType::DSFINALTX, nSessionID, txFinal);
            return true;
        });
        if(!fOk) {
            // no such node? maybe this client disconnected or our own connection went down
            RelayStatus(STATUS_REJECTED, connman);
            break;
        }
    }
}

void CExclusiveSendServer::PushStatus(CNode* pnode, PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID, CConnman& connman)
{
    if(!pnode) return;
    connman.PushMessage(pnode, NetMsgType::DSSTATUSUPDATE, nSessionID, (int)nState, (int)vecEntries.size(), (int)nStatusUpdate, (int)nMessageID);
}

void CExclusiveSendServer::RelayStatus(PoolStatusUpdate nStatusUpdate, CConnman& connman, PoolMessage nMessageID)
{
    unsigned int nDisconnected{};
    // status updates should be relayed to mixing participants only
    for (const auto entry : vecEntries) {
        // make sure everyone is still connected
        bool fOk = connman.ForNode(entry.addr, [&nStatusUpdate, &nMessageID, &connman, this](CNode* pnode) {
            PushStatus(pnode, nStatusUpdate, nMessageID, connman);
            return true;
        });
        if(!fOk) {
            // no such node? maybe this client disconnected or our own connection went down
            ++nDisconnected;
        }
    }
    if (nDisconnected == 0) return; // all is clear

    // smth went wrong
    LogPrintf("CExclusiveSendServer::%s -- can't continue, %llu client(s) disconnected, nSessionID: %d  nSessionDenom: %d (%s)\n",
            __func__, nDisconnected, nSessionID, nSessionDenom, CExclusiveSend::GetDenominationsToString(nSessionDenom));

    // notify everyone else that this session should be terminated
    for (const auto entry : vecEntries) {
        connman.ForNode(entry.addr, [&connman, this](CNode* pnode) {
            PushStatus(pnode, STATUS_REJECTED, MSG_NOERR, connman);
            return true;
        });
    }

    if(nDisconnected == vecEntries.size()) {
        // all clients disconnected, there is probably some issues with our own connection
        // do not charge any fees, just reset the pool
        SetNull();
    }
}

void CExclusiveSendServer::RelayCompletedTransaction(PoolMessage nMessageID, CConnman& connman)
{
    LogPrint("exclusivesend", "CExclusiveSendServer::%s -- nSessionID: %d  nSessionDenom: %d (%s)\n",
            __func__, nSessionID, nSessionDenom, CExclusiveSend::GetDenominationsToString(nSessionDenom));

    // final mixing tx with empty signatures should be relayed to mixing participants only
    for (const auto entry : vecEntries) {
        bool fOk = connman.ForNode(entry.addr, [&nMessageID, &connman, this](CNode* pnode) {
            connman.PushMessage(pnode, NetMsgType::DSCOMPLETE, nSessionID, (int)nMessageID);
            return true;
        });
        if(!fOk) {
            // no such node? maybe client disconnected or our own connection went down
            RelayStatus(STATUS_REJECTED, connman);
            break;
        }
    }
}

void CExclusiveSendServer::SetState(PoolState nStateNew)
{
    if(fMasterNode && (nStateNew == POOL_STATE_ERROR || nStateNew == POOL_STATE_SUCCESS)) {
        LogPrint("exclusivesend", "CExclusiveSendServer::SetState -- Can't set state to ERROR or SUCCESS as a Masternode. \n");
        return;
    }

    LogPrintf("CExclusiveSendServer::SetState -- nState: %d, nStateNew: %d\n", nState, nStateNew);
    nState = nStateNew;
}

//TODO: Rename/move to core
void ThreadCheckExclusiveSendServer(CConnman& connman)
{
    if(fLiteMode) return; // disable all TriveCoin specific functionality

    static bool fOneThread;
    if(fOneThread) return;
    fOneThread = true;

    // Make this thread recognisable as the ExclusiveSend thread
    RenameThread("trivecoin-ps-server");

    unsigned int nTick = 0;

    while (true)
    {
        MilliSleep(1000);

        if(masternodeSync.IsBlockchainSynced() && !ShutdownRequested()) {
            nTick++;
            privateSendServer.CheckTimeout(connman);
            privateSendServer.CheckForCompleteQueue(connman);
        }
    }
}
