// Copyright (c) 2019 The Trivechain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "quorums_chainlocks.h"
#include "quorums_directsend.h"
#include "quorums_utils.h"

#include "bls/bls_batchverifier.h"
#include "chainparams.h"
#include "coins.h"
#include "txmempool.h"
#include "masternode-sync.h"
#include "net_processing.h"
#include "spork.h"
#include "validation.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

// needed for AUTO_IX_MEMPOOL_THRESHOLD
#include "instantx.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>

namespace llmq
{

static const std::string INPUTLOCK_REQUESTID_PREFIX = "inlock";
static const std::string ISLOCK_REQUESTID_PREFIX = "islock";

CDirectSendManager* quorumDirectSendManager;

uint256 CDirectSendLock::GetRequestId() const
{
    CHashWriter hw(SER_GETHASH, 0);
    hw << ISLOCK_REQUESTID_PREFIX;
    hw << inputs;
    return hw.GetHash();
}

////////////////

void CDirectSendDb::WriteNewDirectSendLock(const uint256& hash, const CDirectSendLock& islock)
{
    CDBBatch batch(db);
    batch.Write(std::make_tuple(std::string("is_i"), hash), islock);
    batch.Write(std::make_tuple(std::string("is_tx"), islock.txid), hash);
    for (auto& in : islock.inputs) {
        batch.Write(std::make_tuple(std::string("is_in"), in), hash);
    }
    db.WriteBatch(batch);

    auto p = std::make_shared<CDirectSendLock>(islock);
    islockCache.insert(hash, p);
    txidCache.insert(islock.txid, hash);
    for (auto& in : islock.inputs) {
        outpointCache.insert(in, hash);
    }
}

void CDirectSendDb::RemoveDirectSendLock(CDBBatch& batch, const uint256& hash, CDirectSendLockPtr islock)
{
    if (!islock) {
        islock = GetDirectSendLockByHash(hash);
        if (!islock) {
            return;
        }
    }

    batch.Erase(std::make_tuple(std::string("is_i"), hash));
    batch.Erase(std::make_tuple(std::string("is_tx"), islock->txid));
    for (auto& in : islock->inputs) {
        batch.Erase(std::make_tuple(std::string("is_in"), in));
    }

    islockCache.erase(hash);
    txidCache.erase(islock->txid);
    for (auto& in : islock->inputs) {
        outpointCache.erase(in);
    }
}

static std::tuple<std::string, uint32_t, uint256> BuildInversedISLockKey(const std::string& k, int nHeight, const uint256& islockHash)
{
    return std::make_tuple(k, htobe32(std::numeric_limits<uint32_t>::max() - nHeight), islockHash);
}

void CDirectSendDb::WriteDirectSendLockMined(const uint256& hash, int nHeight)
{
    db.Write(BuildInversedISLockKey("is_m", nHeight, hash), true);
}

void CDirectSendDb::RemoveDirectSendLockMined(const uint256& hash, int nHeight)
{
    db.Erase(BuildInversedISLockKey("is_m", nHeight, hash));
}

void CDirectSendDb::WriteDirectSendLockArchived(CDBBatch& batch, const uint256& hash, int nHeight)
{
    batch.Write(BuildInversedISLockKey("is_a1", nHeight, hash), true);
    batch.Write(std::make_tuple(std::string("is_a2"), hash), true);
}

std::unordered_map<uint256, CDirectSendLockPtr> CDirectSendDb::RemoveConfirmedDirectSendLocks(int nUntilHeight)
{
    auto it = std::unique_ptr<CDBIterator>(db.NewIterator());

    auto firstKey = BuildInversedISLockKey("is_m", nUntilHeight, uint256());

    it->Seek(firstKey);

    CDBBatch batch(db);
    std::unordered_map<uint256, CDirectSendLockPtr> ret;
    while (it->Valid()) {
        decltype(firstKey) curKey;
        if (!it->GetKey(curKey) || std::get<0>(curKey) != "is_m") {
            break;
        }
        uint32_t nHeight = std::numeric_limits<uint32_t>::max() - be32toh(std::get<1>(curKey));
        if (nHeight > nUntilHeight) {
            break;
        }

        auto& islockHash = std::get<2>(curKey);
        auto islock = GetDirectSendLockByHash(islockHash);
        if (islock) {
            RemoveDirectSendLock(batch, islockHash, islock);
            ret.emplace(islockHash, islock);
        }

        // archive the islock hash, so that we're still able to check if we've seen the islock in the past
        WriteDirectSendLockArchived(batch, islockHash, nHeight);

        batch.Erase(curKey);

        it->Next();
    }

    db.WriteBatch(batch);

    return ret;
}

void CDirectSendDb::RemoveArchivedDirectSendLocks(int nUntilHeight)
{
    auto it = std::unique_ptr<CDBIterator>(db.NewIterator());

    auto firstKey = BuildInversedISLockKey("is_a1", nUntilHeight, uint256());

    it->Seek(firstKey);

    CDBBatch batch(db);
    while (it->Valid()) {
        decltype(firstKey) curKey;
        if (!it->GetKey(curKey) || std::get<0>(curKey) != "is_a1") {
            break;
        }
        uint32_t nHeight = std::numeric_limits<uint32_t>::max() - be32toh(std::get<1>(curKey));
        if (nHeight > nUntilHeight) {
            break;
        }

        auto& islockHash = std::get<2>(curKey);
        batch.Erase(std::make_tuple(std::string("is_a2"), islockHash));
        batch.Erase(curKey);

        it->Next();
    }

    db.WriteBatch(batch);
}

bool CDirectSendDb::HasArchivedDirectSendLock(const uint256& islockHash)
{
    return db.Exists(std::make_tuple(std::string("is_a2"), islockHash));
}

size_t CDirectSendDb::GetDirectSendLockCount()
{
    auto it = std::unique_ptr<CDBIterator>(db.NewIterator());
    auto firstKey = std::make_tuple(std::string("is_i"), uint256());

    it->Seek(firstKey);

    size_t cnt = 0;
    while (it->Valid()) {
        decltype(firstKey) curKey;
        if (!it->GetKey(curKey) || std::get<0>(curKey) != "is_i") {
            break;
        }

        cnt++;

        it->Next();
    }

    return cnt;
}

CDirectSendLockPtr CDirectSendDb::GetDirectSendLockByHash(const uint256& hash)
{
    CDirectSendLockPtr ret;
    if (islockCache.get(hash, ret)) {
        return ret;
    }

    ret = std::make_shared<CDirectSendLock>();
    bool exists = db.Read(std::make_tuple(std::string("is_i"), hash), *ret);
    if (!exists) {
        ret = nullptr;
    }
    islockCache.insert(hash, ret);
    return ret;
}

uint256 CDirectSendDb::GetDirectSendLockHashByTxid(const uint256& txid)
{
    uint256 islockHash;

    bool found = txidCache.get(txid, islockHash);
    if (found && islockHash.IsNull()) {
        return uint256();
    }

    if (!found) {
        found = db.Read(std::make_tuple(std::string("is_tx"), txid), islockHash);
        txidCache.insert(txid, islockHash);
    }

    if (!found) {
        return uint256();
    }
    return islockHash;
}

CDirectSendLockPtr CDirectSendDb::GetDirectSendLockByTxid(const uint256& txid)
{
    uint256 islockHash = GetDirectSendLockHashByTxid(txid);
    if (islockHash.IsNull()) {
        return nullptr;
    }
    return GetDirectSendLockByHash(islockHash);
}

CDirectSendLockPtr CDirectSendDb::GetDirectSendLockByInput(const COutPoint& outpoint)
{
    uint256 islockHash;
    bool found = outpointCache.get(outpoint, islockHash);
    if (found && islockHash.IsNull()) {
        return nullptr;
    }

    if (!found) {
        found = db.Read(std::make_tuple(std::string("is_in"), outpoint), islockHash);
        outpointCache.insert(outpoint, islockHash);
    }

    if (!found) {
        return nullptr;
    }
    return GetDirectSendLockByHash(islockHash);
}

std::vector<uint256> CDirectSendDb::GetDirectSendLocksByParent(const uint256& parent)
{
    auto it = std::unique_ptr<CDBIterator>(db.NewIterator());
    auto firstKey = std::make_tuple(std::string("is_in"), COutPoint(parent, 0));
    it->Seek(firstKey);

    std::vector<uint256> result;

    while (it->Valid()) {
        decltype(firstKey) curKey;
        if (!it->GetKey(curKey) || std::get<0>(curKey) != "is_in") {
            break;
        }
        auto& outpoint = std::get<1>(curKey);
        if (outpoint.hash != parent) {
            break;
        }

        uint256 islockHash;
        if (!it->GetValue(islockHash)) {
            break;
        }
        result.emplace_back(islockHash);
        it->Next();
    }

    return result;
}

std::vector<uint256> CDirectSendDb::RemoveChainedDirectSendLocks(const uint256& islockHash, const uint256& txid, int nHeight)
{
    std::vector<uint256> result;

    std::vector<uint256> stack;
    std::unordered_set<uint256, StaticSaltedHasher> added;
    stack.emplace_back(txid);

    CDBBatch batch(db);
    while (!stack.empty()) {
        auto children = GetDirectSendLocksByParent(stack.back());
        stack.pop_back();

        for (auto& childIslockHash : children) {
            auto childIsLock = GetDirectSendLockByHash(childIslockHash);
            if (!childIsLock) {
                continue;
            }

            RemoveDirectSendLock(batch, childIslockHash, childIsLock);
            WriteDirectSendLockArchived(batch, childIslockHash, nHeight);
            result.emplace_back(childIslockHash);

            if (added.emplace(childIsLock->txid).second) {
                stack.emplace_back(childIsLock->txid);
            }
        }
    }

    RemoveDirectSendLock(batch, islockHash, nullptr);
    WriteDirectSendLockArchived(batch, islockHash, nHeight);
    result.emplace_back(islockHash);

    db.WriteBatch(batch);

    return result;
}

////////////////

CDirectSendManager::CDirectSendManager(CDBWrapper& _llmqDb) :
    db(_llmqDb)
{
    workInterrupt.reset();
}

CDirectSendManager::~CDirectSendManager()
{
}

void CDirectSendManager::Start()
{
    // can't start new thread if we have one running already
    if (workThread.joinable()) {
        assert(false);
    }

    workThread = std::thread(&TraceThread<std::function<void()> >, "directsend", std::function<void()>(std::bind(&CDirectSendManager::WorkThreadMain, this)));

    quorumSigningManager->RegisterRecoveredSigsListener(this);
}

void CDirectSendManager::Stop()
{
    quorumSigningManager->UnregisterRecoveredSigsListener(this);

    // make sure to call InterruptWorkerThread() first
    if (!workInterrupt) {
        assert(false);
    }

    if (workThread.joinable()) {
        workThread.join();
    }
}

void CDirectSendManager::InterruptWorkerThread()
{
    workInterrupt();
}

bool CDirectSendManager::ProcessTx(const CTransaction& tx, const Consensus::Params& params)
{
    if (!IsNewDirectSendEnabled()) {
        return true;
    }

    auto llmqType = params.llmqForDirectSend;
    if (llmqType == Consensus::LLMQ_NONE) {
        return true;
    }
    if (!fMasternodeMode) {
        return true;
    }

    // Ignore any DirectSend messages until blockchain is synced
    if (!masternodeSync.IsBlockchainSynced()) {
        return true;
    }

    // In case the islock was received before the TX, filtered announcement might have missed this islock because
    // we were unable to check for filter matches deep inside the TX. Now we have the TX, so we should retry.
    uint256 islockHash;
    {
        LOCK(cs);
        islockHash = db.GetDirectSendLockHashByTxid(tx.GetHash());
    }
    if (!islockHash.IsNull()) {
        CInv inv(MSG_ISLOCK, islockHash);
        g_connman->RelayInvFiltered(inv, tx, LLMQS_PROTO_VERSION);
    }

    if (IsConflicted(tx)) {
        return false;
    }

    if (!CheckCanLock(tx, true, params)) {
        return false;
    }

    std::vector<uint256> ids;
    ids.reserve(tx.vin.size());

    size_t alreadyVotedCount = 0;
    for (const auto& in : tx.vin) {
        auto id = ::SerializeHash(std::make_pair(INPUTLOCK_REQUESTID_PREFIX, in.prevout));
        ids.emplace_back(id);

        uint256 otherTxHash;
        if (quorumSigningManager->GetVoteForId(llmqType, id, otherTxHash)) {
            if (otherTxHash != tx.GetHash()) {
                LogPrintf("CDirectSendManager::%s -- txid=%s: input %s is conflicting with islock %s\n", __func__,
                         tx.GetHash().ToString(), in.prevout.ToStringShort(), otherTxHash.ToString());
                return false;
            }
            alreadyVotedCount++;
        }

        // don't even try the actual signing if any input is conflicting
        if (quorumSigningManager->IsConflicting(llmqType, id, tx.GetHash())) {
            return false;
        }
    }
    if (alreadyVotedCount == ids.size()) {
        return true;
    }

    for (size_t i = 0; i < tx.vin.size(); i++) {
        auto& in = tx.vin[i];
        auto& id = ids[i];
        inputRequestIds.emplace(id);
        if (quorumSigningManager->AsyncSignIfMember(llmqType, id, tx.GetHash())) {
            LogPrintf("CDirectSendManager::%s -- txid=%s: voted on input %s with id %s\n", __func__,
                      tx.GetHash().ToString(), in.prevout.ToStringShort(), id.ToString());
        }
    }

    // We might have received all input locks before we got the corresponding TX. In this case, we have to sign the
    // islock now instead of waiting for the input locks.
    TrySignDirectSendLock(tx);

    return true;
}

bool CDirectSendManager::CheckCanLock(const CTransaction& tx, bool printDebug, const Consensus::Params& params)
{
    if (tx.vin.empty()) {
        // can't lock TXs without inputs (e.g. quorum commitments)
        return false;
    }

    CAmount nValueIn = 0;
    for (const auto& in : tx.vin) {
        CAmount v = 0;
        if (!CheckCanLock(in.prevout, printDebug, tx.GetHash(), &v, params)) {
            return false;
        }

        nValueIn += v;
    }

    // TODO decide if we should limit max input values. This was ok to do in the old system, but in the new system
    // where we want to have all TXs locked at some point, this is counterproductive (especially when ChainLocks later
    // depend on all TXs being locked first)
//    CAmount maxValueIn = sporkManager.GetSporkValue(SPORK_5_DIRECTSEND_MAX_VALUE);
//    if (nValueIn > maxValueIn * COIN) {
//        if (printDebug) {
//            LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: TX input value too high. nValueIn=%f, maxValueIn=%d", __func__,
//                     tx.GetHash().ToString(), nValueIn / (double)COIN, maxValueIn);
//        }
//        return false;
//    }

    return true;
}

bool CDirectSendManager::CheckCanLock(const COutPoint& outpoint, bool printDebug, const uint256& txHash, CAmount* retValue, const Consensus::Params& params)
{
    int nDirectSendConfirmationsRequired = params.nDirectSendConfirmationsRequired;

    if (IsLocked(outpoint.hash)) {
        // if prevout was ix locked, allow locking of descendants (no matter if prevout is in mempool or already mined)
        return true;
    }

    auto mempoolTx = mempool.get(outpoint.hash);
    if (mempoolTx) {
        if (printDebug) {
            LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: parent mempool TX %s is not locked\n", __func__,
                     txHash.ToString(), outpoint.hash.ToString());
        }
        return false;
    }

    CTransactionRef tx;
    uint256 hashBlock;
    // this relies on enabled txindex and won't work if we ever try to remove the requirement for txindex for masternodes
    if (!GetTransaction(outpoint.hash, tx, params, hashBlock, false)) {
        if (printDebug) {
            LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: failed to find parent TX %s\n", __func__,
                     txHash.ToString(), outpoint.hash.ToString());
        }
        return false;
    }

    const CBlockIndex* pindexMined;
    int nTxAge;
    {
        LOCK(cs_main);
        pindexMined = mapBlockIndex.at(hashBlock);
        nTxAge = chainActive.Height() - pindexMined->nHeight + 1;
    }

    if (nTxAge < nDirectSendConfirmationsRequired) {
        if (!llmq::chainLocksHandler->HasChainLock(pindexMined->nHeight, pindexMined->GetBlockHash())) {
            if (printDebug) {
                LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: outpoint %s too new and not ChainLocked. nTxAge=%d, nDirectSendConfirmationsRequired=%d\n", __func__,
                         txHash.ToString(), outpoint.ToStringShort(), nTxAge, nDirectSendConfirmationsRequired);
            }
            return false;
        }
    }

    if (retValue) {
        *retValue = tx->vout[outpoint.n].nValue;
    }

    return true;
}

void CDirectSendManager::HandleNewRecoveredSig(const CRecoveredSig& recoveredSig)
{
    if (!IsNewDirectSendEnabled()) {
        return;
    }

    auto llmqType = Params().GetConsensus().llmqForDirectSend;
    if (llmqType == Consensus::LLMQ_NONE) {
        return;
    }
    auto& params = Params().GetConsensus().llmqs.at(llmqType);

    uint256 txid;
    bool isDirectSendLock = false;
    {
        LOCK(cs);
        if (inputRequestIds.count(recoveredSig.id)) {
            txid = recoveredSig.msgHash;
        }
        if (creatingDirectSendLocks.count(recoveredSig.id)) {
            isDirectSendLock = true;
        }
    }
    if (!txid.IsNull()) {
        HandleNewInputLockRecoveredSig(recoveredSig, txid);
    } else if (isDirectSendLock) {
        HandleNewDirectSendLockRecoveredSig(recoveredSig);
    }
}

void CDirectSendManager::HandleNewInputLockRecoveredSig(const CRecoveredSig& recoveredSig, const uint256& txid)
{
    auto llmqType = Params().GetConsensus().llmqForDirectSend;

    CTransactionRef tx;
    uint256 hashBlock;
    if (!GetTransaction(txid, tx, Params().GetConsensus(), hashBlock, true)) {
        return;
    }

    if (LogAcceptCategory("directsend")) {
        for (auto& in : tx->vin) {
            auto id = ::SerializeHash(std::make_pair(INPUTLOCK_REQUESTID_PREFIX, in.prevout));
            if (id == recoveredSig.id) {
                LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: got recovered sig for input %s\n", __func__,
                         txid.ToString(), in.prevout.ToStringShort());
                break;
            }
        }
    }

    TrySignDirectSendLock(*tx);
}

void CDirectSendManager::TrySignDirectSendLock(const CTransaction& tx)
{
    auto llmqType = Params().GetConsensus().llmqForDirectSend;

    for (auto& in : tx.vin) {
        auto id = ::SerializeHash(std::make_pair(INPUTLOCK_REQUESTID_PREFIX, in.prevout));
        if (!quorumSigningManager->HasRecoveredSig(llmqType, id, tx.GetHash())) {
            return;
        }
    }

    LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: got all recovered sigs, creating CDirectSendLock\n", __func__,
            tx.GetHash().ToString());

    CDirectSendLock islock;
    islock.txid = tx.GetHash();
    for (auto& in : tx.vin) {
        islock.inputs.emplace_back(in.prevout);
    }

    auto id = islock.GetRequestId();

    if (quorumSigningManager->HasRecoveredSigForId(llmqType, id)) {
        return;
    }

    {
        LOCK(cs);
        auto e = creatingDirectSendLocks.emplace(id, std::move(islock));
        if (!e.second) {
            return;
        }
        txToCreatingDirectSendLocks.emplace(tx.GetHash(), &e.first->second);
    }

    quorumSigningManager->AsyncSignIfMember(llmqType, id, tx.GetHash());
}

void CDirectSendManager::HandleNewDirectSendLockRecoveredSig(const llmq::CRecoveredSig& recoveredSig)
{
    CDirectSendLock islock;

    {
        LOCK(cs);
        auto it = creatingDirectSendLocks.find(recoveredSig.id);
        if (it == creatingDirectSendLocks.end()) {
            return;
        }

        islock = std::move(it->second);
        creatingDirectSendLocks.erase(it);
        txToCreatingDirectSendLocks.erase(islock.txid);
    }

    if (islock.txid != recoveredSig.msgHash) {
        LogPrintf("CDirectSendManager::%s -- txid=%s: islock conflicts with %s, dropping own version", __func__,
                islock.txid.ToString(), recoveredSig.msgHash.ToString());
        return;
    }

    islock.sig = recoveredSig.sig;
    ProcessDirectSendLock(-1, ::SerializeHash(islock), islock);
}

void CDirectSendManager::ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman)
{
    if (!IsNewDirectSendEnabled()) {
        return;
    }

    if (strCommand == NetMsgType::ISLOCK) {
        CDirectSendLock islock;
        vRecv >> islock;
        ProcessMessageDirectSendLock(pfrom, islock, connman);
    }
}

void CDirectSendManager::ProcessMessageDirectSendLock(CNode* pfrom, const llmq::CDirectSendLock& islock, CConnman& connman)
{
    bool ban = false;
    if (!PreVerifyDirectSendLock(pfrom->id, islock, ban)) {
        if (ban) {
            LOCK(cs_main);
            Misbehaving(pfrom->id, 100);
        }
        return;
    }

    auto hash = ::SerializeHash(islock);

    LOCK(cs);
    if (db.GetDirectSendLockByHash(hash) != nullptr) {
        return;
    }
    if (pendingDirectSendLocks.count(hash)) {
        return;
    }

    LogPrint("directsend", "CDirectSendManager::%s -- txid=%s, islock=%s: received islock, peer=%d\n", __func__,
            islock.txid.ToString(), hash.ToString(), pfrom->id);

    pendingDirectSendLocks.emplace(hash, std::make_pair(pfrom->id, std::move(islock)));
}

bool CDirectSendManager::PreVerifyDirectSendLock(NodeId nodeId, const llmq::CDirectSendLock& islock, bool& retBan)
{
    retBan = false;

    if (islock.txid.IsNull() || islock.inputs.empty()) {
        retBan = true;
        return false;
    }

    std::set<COutPoint> dups;
    for (auto& o : islock.inputs) {
        if (!dups.emplace(o).second) {
            retBan = true;
            return false;
        }
    }

    return true;
}

bool CDirectSendManager::ProcessPendingDirectSendLocks()
{
    decltype(pendingDirectSendLocks) pend;

    {
        LOCK(cs);
        pend = std::move(pendingDirectSendLocks);
    }

    if (pend.empty()) {
        return false;
    }

    if (!IsNewDirectSendEnabled()) {
        return false;
    }

    int tipHeight;
    {
        LOCK(cs_main);
        tipHeight = chainActive.Height();
    }

    auto llmqType = Params().GetConsensus().llmqForDirectSend;

    // Every time a new quorum enters the active set, an older one is removed. This means that between two blocks, the
    // active set can be different, leading to different selection of the signing quorum. When we detect such rotation
    // of the active set, we must re-check invalid sigs against the previous active set and only ban nodes when this also
    // fails.
    auto quorums1 = quorumSigningManager->GetActiveQuorumSet(llmqType, tipHeight);
    auto quorums2 = quorumSigningManager->GetActiveQuorumSet(llmqType, tipHeight - 1);
    bool quorumsRotated = quorums1 != quorums2;

    if (quorumsRotated) {
        // first check against the current active set and don't ban
        auto badISLocks = ProcessPendingDirectSendLocks(tipHeight, pend, false);
        if (!badISLocks.empty()) {
            LogPrintf("CDirectSendManager::%s -- detected LLMQ active set rotation, redoing verification on old active set\n", __func__);

            // filter out valid IS locks from "pend"
            for (auto it = pend.begin(); it != pend.end(); ) {
                if (!badISLocks.count(it->first)) {
                    it = pend.erase(it);
                } else {
                    ++it;
                }
            }
            // now check against the previous active set and perform banning if this fails
            ProcessPendingDirectSendLocks(tipHeight - 1, pend, true);
        }
    } else {
        ProcessPendingDirectSendLocks(tipHeight, pend, true);
    }

    return true;
}

std::unordered_set<uint256> CDirectSendManager::ProcessPendingDirectSendLocks(int signHeight, const std::unordered_map<uint256, std::pair<NodeId, CDirectSendLock>>& pend, bool ban)
{
    auto llmqType = Params().GetConsensus().llmqForDirectSend;

    CBLSBatchVerifier<NodeId, uint256> batchVerifier(false, true, 8);
    std::unordered_map<uint256, std::pair<CQuorumCPtr, CRecoveredSig>> recSigs;

    for (const auto& p : pend) {
        auto& hash = p.first;
        auto nodeId = p.second.first;
        auto& islock = p.second.second;

        if (batchVerifier.badSources.count(nodeId)) {
            continue;
        }

        if (!islock.sig.Get().IsValid()) {
            batchVerifier.badSources.emplace(nodeId);
            continue;
        }

        auto id = islock.GetRequestId();

        // no need to verify an ISLOCK if we already have verified the recovered sig that belongs to it
        if (quorumSigningManager->HasRecoveredSig(llmqType, id, islock.txid)) {
            continue;
        }

        auto quorum = quorumSigningManager->SelectQuorumForSigning(llmqType, signHeight, id);
        if (!quorum) {
            // should not happen, but if one fails to select, all others will also fail to select
            return {};
        }
        uint256 signHash = CLLMQUtils::BuildSignHash(llmqType, quorum->qc.quorumHash, id, islock.txid);
        batchVerifier.PushMessage(nodeId, hash, signHash, islock.sig.Get(), quorum->qc.quorumPublicKey);

        // We can reconstruct the CRecoveredSig objects from the islock and pass it to the signing manager, which
        // avoids unnecessary double-verification of the signature. We however only do this when verification here
        // turns out to be good (which is checked further down)
        if (!quorumSigningManager->HasRecoveredSigForId(llmqType, id)) {
            CRecoveredSig recSig;
            recSig.llmqType = llmqType;
            recSig.quorumHash = quorum->qc.quorumHash;
            recSig.id = id;
            recSig.msgHash = islock.txid;
            recSig.sig = islock.sig;
            recSigs.emplace(std::piecewise_construct,
                    std::forward_as_tuple(hash),
                    std::forward_as_tuple(std::move(quorum), std::move(recSig)));
        }
    }

    batchVerifier.Verify();

    std::unordered_set<uint256> badISLocks;

    if (ban && !batchVerifier.badSources.empty()) {
        LOCK(cs_main);
        for (auto& nodeId : batchVerifier.badSources) {
            // Let's not be too harsh, as the peer might simply be unlucky and might have sent us an old lock which
            // does not validate anymore due to changed quorums
            Misbehaving(nodeId, 20);
        }
    }
    for (const auto& p : pend) {
        auto& hash = p.first;
        auto nodeId = p.second.first;
        auto& islock = p.second.second;

        if (batchVerifier.badMessages.count(hash)) {
            LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: invalid sig in islock, peer=%d\n", __func__,
                     islock.txid.ToString(), hash.ToString(), nodeId);
            badISLocks.emplace(hash);
            continue;
        }

        ProcessDirectSendLock(nodeId, hash, islock);

        // See comment further on top. We pass a reconstructed recovered sig to the signing manager to avoid
        // double-verification of the sig.
        auto it = recSigs.find(hash);
        if (it != recSigs.end()) {
            auto& quorum = it->second.first;
            auto& recSig = it->second.second;
            if (!quorumSigningManager->HasRecoveredSigForId(llmqType, recSig.id)) {
                recSig.UpdateHash();
                LogPrint("directsend", "CDirectSendManager::%s -- txid=%s, islock=%s: passing reconstructed recSig to signing mgr, peer=%d\n", __func__,
                         islock.txid.ToString(), hash.ToString(), nodeId);
                quorumSigningManager->PushReconstructedRecoveredSig(recSig, quorum);
            }
        }
    }

    return badISLocks;
}

void CDirectSendManager::ProcessDirectSendLock(NodeId from, const uint256& hash, const CDirectSendLock& islock)
{
    {
        LOCK(cs_main);
        g_connman->RemoveAskFor(hash);
    }

    CTransactionRef tx;
    uint256 hashBlock;
    const CBlockIndex* pindexMined = nullptr;
    // we ignore failure here as we must be able to propagate the lock even if we don't have the TX locally
    if (GetTransaction(islock.txid, tx, Params().GetConsensus(), hashBlock)) {
        if (!hashBlock.IsNull()) {
            {
                LOCK(cs_main);
                pindexMined = mapBlockIndex.at(hashBlock);
            }

            // Let's see if the TX that was locked by this islock is already mined in a ChainLocked block. If yes,
            // we can simply ignore the islock, as the ChainLock implies locking of all TXs in that chain
            if (llmq::chainLocksHandler->HasChainLock(pindexMined->nHeight, pindexMined->GetBlockHash())) {
                LogPrint("directsend", "CDirectSendManager::%s -- txlock=%s, islock=%s: dropping islock as it already got a ChainLock in block %s, peer=%d\n", __func__,
                         islock.txid.ToString(), hash.ToString(), hashBlock.ToString(), from);
                return;
            }
        }
    }

    {
        LOCK(cs);

        LogPrint("directsend", "CDirectSendManager::%s -- txid=%s, islock=%s: processsing islock, peer=%d\n", __func__,
                 islock.txid.ToString(), hash.ToString(), from);

        creatingDirectSendLocks.erase(islock.GetRequestId());
        txToCreatingDirectSendLocks.erase(islock.txid);

        CDirectSendLockPtr otherIsLock;
        if (db.GetDirectSendLockByHash(hash)) {
            return;
        }
        otherIsLock = db.GetDirectSendLockByTxid(islock.txid);
        if (otherIsLock != nullptr) {
            LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: duplicate islock, other islock=%s, peer=%d\n", __func__,
                     islock.txid.ToString(), hash.ToString(), ::SerializeHash(*otherIsLock).ToString(), from);
        }
        for (auto& in : islock.inputs) {
            otherIsLock = db.GetDirectSendLockByInput(in);
            if (otherIsLock != nullptr) {
                LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: conflicting input in islock. input=%s, other islock=%s, peer=%d\n", __func__,
                         islock.txid.ToString(), hash.ToString(), in.ToStringShort(), ::SerializeHash(*otherIsLock).ToString(), from);
            }
        }

        db.WriteNewDirectSendLock(hash, islock);
        if (pindexMined) {
            db.WriteDirectSendLockMined(hash, pindexMined->nHeight);
        }

        // This will also add children TXs to pendingRetryTxs
        RemoveNonLockedTx(islock.txid, true);
    }

    CInv inv(MSG_ISLOCK, hash);
    if (tx != nullptr) {
        g_connman->RelayInvFiltered(inv, *tx, LLMQS_PROTO_VERSION);
    } else {
        // we don't have the TX yet, so we only filter based on txid. Later when that TX arrives, we will re-announce
        // with the TX taken into account.
        g_connman->RelayInvFiltered(inv, islock.txid, LLMQS_PROTO_VERSION);
    }

    RemoveMempoolConflictsForLock(hash, islock);
    ResolveBlockConflicts(hash, islock);
    UpdateWalletTransaction(islock.txid, tx);
}

void CDirectSendManager::UpdateWalletTransaction(const uint256& txid, const CTransactionRef& tx)
{
#ifdef ENABLE_WALLET
    if (!pwalletMain) {
        return;
    }

    if (pwalletMain->UpdatedTransaction(txid)) {
        // notify an external script once threshold is reached
        std::string strCmd = GetArg("-directsendnotify", "");
        if (!strCmd.empty()) {
            boost::replace_all(strCmd, "%s", txid.GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }
    }
#endif

    if (tx) {
        GetMainSignals().NotifyTransactionLock(*tx);
        // bump mempool counter to make sure newly mined txes are picked up by getblocktemplate
        mempool.AddTransactionsUpdated(1);
    }
}

void CDirectSendManager::SyncTransaction(const CTransaction& tx, const CBlockIndex* pindex, int posInBlock)
{
    if (!IsNewDirectSendEnabled()) {
        return;
    }

    if (tx.IsCoinBase() || tx.vin.empty()) {
        // coinbase can't and TXs with no inputs be locked
        return;
    }

    bool inMempool = mempool.get(tx.GetHash()) != nullptr;
    bool isDisconnect = pindex && posInBlock == CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK;

    // Are we called from validation.cpp/MemPoolConflictRemovalTracker?
    // TODO refactor this when we backport the BlockConnected signal from Bitcoin, as it gives better info about
    // conflicted TXs
    bool isConflictRemoved = isDisconnect && !inMempool;

    if (isConflictRemoved) {
        LOCK(cs);
        RemoveConflictedTx(tx);
        return;
    }

    uint256 islockHash;
    {
        LOCK(cs);
        islockHash = db.GetDirectSendLockHashByTxid(tx.GetHash());

        // update DB about when an IS lock was mined
        if (!islockHash.IsNull() && pindex) {
            if (isDisconnect) {
                // SyncTransaction is called with pprev
                db.RemoveDirectSendLockMined(islockHash, pindex->nHeight + 1);
            } else {
                db.WriteDirectSendLockMined(islockHash, pindex->nHeight);
            }
        }
    }

    if (!masternodeSync.IsBlockchainSynced()) {
        return;
    }

    bool chainlocked = pindex && chainLocksHandler->HasChainLock(pindex->nHeight, pindex->GetBlockHash());
    if (islockHash.IsNull() && !chainlocked) {
        ProcessTx(tx, Params().GetConsensus());
    }

    LOCK(cs);
    if (!chainlocked && islockHash.IsNull()) {
        // TX is not locked, so make sure it is tracked
        AddNonLockedTx(MakeTransactionRef(tx));
        nonLockedTxs.at(tx.GetHash()).pindexMined = !isDisconnect ? pindex : nullptr;
    } else {
        // TX is locked, so make sure we don't track it anymore
        RemoveNonLockedTx(tx.GetHash(), true);
    }
}

void CDirectSendManager::AddNonLockedTx(const CTransactionRef& tx)
{
    AssertLockHeld(cs);
    auto res = nonLockedTxs.emplace(tx->GetHash(), NonLockedTxInfo());
    auto& info = res.first->second;

    if (!info.tx) {
        info.tx = tx;
        for (const auto& in : tx->vin) {
            nonLockedTxs[in.prevout.hash].children.emplace(tx->GetHash());
        }
    }

    if (res.second) {
        for (auto& in : tx->vin) {
            nonLockedTxsByInputs.emplace(in.prevout.hash, std::make_pair(in.prevout.n, tx->GetHash()));
        }
    }
}

void CDirectSendManager::RemoveNonLockedTx(const uint256& txid, bool retryChildren)
{
    AssertLockHeld(cs);

    auto it = nonLockedTxs.find(txid);
    if (it == nonLockedTxs.end()) {
        return;
    }
    auto& info = it->second;

    if (retryChildren) {
        // TX got locked, so we can retry locking children
        for (auto& childTxid : info.children) {
            pendingRetryTxs.emplace(childTxid);
        }
    }

    if (info.tx) {
        for (const auto& in : info.tx->vin) {
            auto jt = nonLockedTxs.find(in.prevout.hash);
            if (jt != nonLockedTxs.end()) {
                jt->second.children.erase(txid);
                if (!jt->second.tx && jt->second.children.empty()) {
                    nonLockedTxs.erase(jt);
                }
            }

            auto its = nonLockedTxsByInputs.equal_range(in.prevout.hash);
            for (auto kt = its.first; kt != its.second; ) {
                if (kt->second.first != in.prevout.n) {
                    ++kt;
                    continue;
                } else {
                    kt = nonLockedTxsByInputs.erase(kt);
                }
            }
        }
    }

    nonLockedTxs.erase(it);
}

void CDirectSendManager::RemoveConflictedTx(const CTransaction& tx)
{
    AssertLockHeld(cs);
    RemoveNonLockedTx(tx.GetHash(), false);

    for (const auto& in : tx.vin) {
        auto inputRequestId = ::SerializeHash(std::make_pair(INPUTLOCK_REQUESTID_PREFIX, in));
        inputRequestIds.erase(inputRequestId);
    }
}

void CDirectSendManager::NotifyChainLock(const CBlockIndex* pindexChainLock)
{
    HandleFullyConfirmedBlock(pindexChainLock);
}

void CDirectSendManager::UpdatedBlockTip(const CBlockIndex* pindexNew)
{
    // TODO remove this after DIP8 has activated
    bool fDIP0008Active = VersionBitsState(pindexNew->pprev, Params().GetConsensus(), Consensus::DEPLOYMENT_DIP0008, versionbitscache) == THRESHOLD_ACTIVE;

    if (sporkManager.IsSporkActive(SPORK_19_CHAINLOCKS_ENABLED) && fDIP0008Active) {
        // Nothing to do here. We should keep all islocks and let chainlocks handle them.
        return;
    }

    int nConfirmedHeight = pindexNew->nHeight - Params().GetConsensus().nDirectSendKeepLock;
    const CBlockIndex* pindex = pindexNew->GetAncestor(nConfirmedHeight);

    if (pindex) {
        HandleFullyConfirmedBlock(pindex);
    }
}

void CDirectSendManager::HandleFullyConfirmedBlock(const CBlockIndex* pindex)
{
    auto& consensusParams = Params().GetConsensus();

    std::unordered_map<uint256, CDirectSendLockPtr> removeISLocks;
    {
        LOCK(cs);

        removeISLocks = db.RemoveConfirmedDirectSendLocks(pindex->nHeight);
        if (pindex->nHeight > 100) {
            db.RemoveArchivedDirectSendLocks(pindex->nHeight - 100);
        }
        for (auto& p : removeISLocks) {
            auto& islockHash = p.first;
            auto& islock = p.second;
            LogPrint("directsend", "CDirectSendManager::%s -- txid=%s, islock=%s: removed islock as it got fully confirmed\n", __func__,
                     islock->txid.ToString(), islockHash.ToString());

            for (auto& in : islock->inputs) {
                auto inputRequestId = ::SerializeHash(std::make_pair(INPUTLOCK_REQUESTID_PREFIX, in));
                inputRequestIds.erase(inputRequestId);

                // no need to keep recovered sigs for fully confirmed IS locks, as there is no chance for conflicts
                // from now on. All inputs are spent now and can't be spend in any other TX.
                quorumSigningManager->RemoveRecoveredSig(consensusParams.llmqForDirectSend, inputRequestId);
            }

            // same as in the loop
            quorumSigningManager->RemoveRecoveredSig(consensusParams.llmqForDirectSend, islock->GetRequestId());
        }

        // Find all previously unlocked TXs that got locked by this fully confirmed (ChainLock) block and remove them
        // from the nonLockedTxs map. Also collect all children of these TXs and mark them for retrying of IS locking.
        std::vector<uint256> toRemove;
        for (auto& p : nonLockedTxs) {
            auto pindexMined = p.second.pindexMined;

            if (pindexMined && pindex->GetAncestor(pindexMined->nHeight) == pindexMined) {
                toRemove.emplace_back(p.first);
            }
        }
        for (auto& txid : toRemove) {
            // This will also add children to pendingRetryTxs
            RemoveNonLockedTx(txid, true);
        }
    }
}

void CDirectSendManager::RemoveMempoolConflictsForLock(const uint256& hash, const CDirectSendLock& islock)
{
    std::unordered_map<uint256, CTransactionRef> toDelete;

    {
        LOCK(mempool.cs);

        for (auto& in : islock.inputs) {
            auto it = mempool.mapNextTx.find(in);
            if (it == mempool.mapNextTx.end()) {
                continue;
            }
            if (it->second->GetHash() != islock.txid) {
                toDelete.emplace(it->second->GetHash(), mempool.get(it->second->GetHash()));

                LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: mempool TX %s with input %s conflicts with islock\n", __func__,
                         islock.txid.ToString(), hash.ToString(), it->second->GetHash().ToString(), in.ToStringShort());
            }
        }

        for (auto& p : toDelete) {
            mempool.removeRecursive(*p.second, MemPoolRemovalReason::CONFLICT);
        }
    }

    if (!toDelete.empty()) {
        {
            LOCK(cs);
            for (auto& p : toDelete) {
                RemoveConflictedTx(*p.second);
            }
        }
        AskNodesForLockedTx(islock.txid);
    }
}

void CDirectSendManager::ResolveBlockConflicts(const uint256& islockHash, const llmq::CDirectSendLock& islock)
{
    // Lets first collect all non-locked TXs which conflict with the given ISLOCK
    std::unordered_map<const CBlockIndex*, std::unordered_map<uint256, CTransactionRef, StaticSaltedHasher>> conflicts;
    {
        LOCK(cs);
        for (auto& in : islock.inputs) {
            auto its = nonLockedTxsByInputs.equal_range(in.hash);
            for (auto it = its.first; it != its.second; ++it) {
                if (it->second.first != in.n) {
                    continue;
                }
                auto& conflictTxid = it->second.second;
                if (conflictTxid == islock.txid) {
                    continue;
                }
                auto jt = nonLockedTxs.find(conflictTxid);
                if (jt == nonLockedTxs.end()) {
                    continue;
                }
                auto& info = jt->second;
                if (!info.pindexMined || !info.tx) {
                    continue;
                }
                LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: mined TX %s with input %s and mined in block %s conflicts with islock\n", __func__,
                          islock.txid.ToString(), islockHash.ToString(), conflictTxid.ToString(), in.ToStringShort(), info.pindexMined->GetBlockHash().ToString());
                conflicts[info.pindexMined].emplace(conflictTxid, info.tx);
            }
        }
    }

    // Lets see if any of the conflicts was already mined into a ChainLocked block
    bool hasChainLockedConflict = false;
    for (const auto& p : conflicts) {
        auto pindex = p.first;
        if (chainLocksHandler->HasChainLock(pindex->nHeight, pindex->GetBlockHash())) {
            hasChainLockedConflict = true;
            break;
        }
    }

    // If a conflict was mined into a ChainLocked block, then we have no other choice and must prune the ISLOCK and all
    // chained ISLOCKs that build on top of this one. The probability of this is practically zero and can only happen
    // when large parts of the masternode network are controlled by an attacker. In this case we must still find consensus
    // and its better to sacrifice individual ISLOCKs then to sacrifice whole ChainLocks.
    if (hasChainLockedConflict) {
        RemoveChainLockConflictingLock(islockHash, islock);
        return;
    }

    bool activateBestChain = false;
    for (const auto& p : conflicts) {
        auto pindex = p.first;
        {
            LOCK(cs);
            for (auto& p2 : p.second) {
                const auto& tx = *p2.second;
                RemoveConflictedTx(tx);
            }
        }

        LogPrintf("CDirectSendManager::%s -- invalidating block %s\n", __func__, pindex->GetBlockHash().ToString());

        LOCK(cs_main);
        CValidationState state;
        // need non-const pointer
        auto pindex2 = mapBlockIndex.at(pindex->GetBlockHash());
        if (!InvalidateBlock(state, Params(), pindex2)) {
            LogPrintf("CDirectSendManager::%s -- InvalidateBlock failed: %s\n", __func__, FormatStateMessage(state));
            // This should not have happened and we are in a state were it's not safe to continue anymore
            assert(false);
        }
        activateBestChain = true;
    }

    if (activateBestChain) {
        CValidationState state;
        if (!ActivateBestChain(state, Params())) {
            LogPrintf("CChainLocksHandler::%s -- ActivateBestChain failed: %s\n", __func__, FormatStateMessage(state));
            // This should not have happened and we are in a state were it's not safe to continue anymore
            assert(false);
        }
    }
}

void CDirectSendManager::RemoveChainLockConflictingLock(const uint256& islockHash, const llmq::CDirectSendLock& islock)
{
    LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: at least one conflicted TX already got a ChainLock. Removing ISLOCK and its chained children.\n", __func__,
              islock.txid.ToString(), islockHash.ToString());
    int tipHeight;
    {
        LOCK(cs_main);
        tipHeight = chainActive.Height();
    }

    LOCK(cs);
    auto removedIslocks = db.RemoveChainedDirectSendLocks(islockHash, islock.txid, tipHeight);
    for (auto& h : removedIslocks) {
        LogPrintf("CDirectSendManager::%s -- txid=%s, islock=%s: removed (child) ISLOCK %s\n", __func__,
                  islock.txid.ToString(), islockHash.ToString(), h.ToString());
    }
}

void CDirectSendManager::AskNodesForLockedTx(const uint256& txid)
{
    std::vector<CNode*> nodesToAskFor;
    g_connman->ForEachNode([&](CNode* pnode) {
        LOCK(pnode->cs_filter);
        if (pnode->filterInventoryKnown.contains(txid)) {
            pnode->AddRef();
            nodesToAskFor.emplace_back(pnode);
        }
    });
    {
        LOCK(cs_main);
        for (CNode* pnode : nodesToAskFor) {
            LogPrintf("CDirectSendManager::%s -- txid=%s: asking other peer %d for correct TX\n", __func__,
                      txid.ToString(), pnode->id);

            CInv inv(MSG_TX, txid);
            pnode->AskFor(inv);
        }
    }
    for (CNode* pnode : nodesToAskFor) {
        pnode->Release();
    }
}

bool CDirectSendManager::ProcessPendingRetryLockTxs()
{
    decltype(pendingRetryTxs) retryTxs;
    {
        LOCK(cs);
        retryTxs = std::move(pendingRetryTxs);
    }

    if (retryTxs.empty()) {
        return false;
    }

    if (!IsNewDirectSendEnabled()) {
        return false;
    }

    int retryCount = 0;
    for (const auto& txid : retryTxs) {
        CTransactionRef tx;
        {
            LOCK(cs);
            auto it = nonLockedTxs.find(txid);
            if (it == nonLockedTxs.end()) {
                continue;
            }
            tx = it->second.tx;

            if (!tx) {
                continue;
            }

            if (txToCreatingDirectSendLocks.count(tx->GetHash())) {
                // we're already in the middle of locking this one
                continue;
            }
            if (IsLocked(tx->GetHash())) {
                continue;
            }
            if (IsConflicted(*tx)) {
                // should not really happen as we have already filtered these out
                continue;
            }
        }

        // CheckCanLock is already called by ProcessTx, so we should avoid calling it twice. But we also shouldn't spam
        // the logs when retrying TXs that are not ready yet.
        if (LogAcceptCategory("directsend")) {
            if (!CheckCanLock(*tx, false, Params().GetConsensus())) {
                continue;
            }
            LogPrint("directsend", "CDirectSendManager::%s -- txid=%s: retrying to lock\n", __func__,
                     tx->GetHash().ToString());
        }

        ProcessTx(*tx, Params().GetConsensus());
        retryCount++;
    }

    if (retryCount != 0) {
        LOCK(cs);
        LogPrint("directsend", "CDirectSendManager::%s -- retried %d TXs. nonLockedTxs.size=%d\n", __func__,
                 retryCount, nonLockedTxs.size());
    }

    return retryCount != 0;
}

bool CDirectSendManager::AlreadyHave(const CInv& inv)
{
    if (!IsNewDirectSendEnabled()) {
        return true;
    }

    LOCK(cs);
    return db.GetDirectSendLockByHash(inv.hash) != nullptr || pendingDirectSendLocks.count(inv.hash) != 0 || db.HasArchivedDirectSendLock(inv.hash);
}

bool CDirectSendManager::GetDirectSendLockByHash(const uint256& hash, llmq::CDirectSendLock& ret)
{
    if (!IsNewDirectSendEnabled()) {
        return false;
    }

    LOCK(cs);
    auto islock = db.GetDirectSendLockByHash(hash);
    if (!islock) {
        return false;
    }
    ret = *islock;
    return true;
}

bool CDirectSendManager::IsLocked(const uint256& txHash)
{
    if (!IsNewDirectSendEnabled()) {
        return false;
    }

    LOCK(cs);
    return db.GetDirectSendLockByTxid(txHash) != nullptr;
}

bool CDirectSendManager::IsConflicted(const CTransaction& tx)
{
    return GetConflictingLock(tx) != nullptr;
}

CDirectSendLockPtr CDirectSendManager::GetConflictingLock(const CTransaction& tx)
{
    if (!IsNewDirectSendEnabled()) {
        return nullptr;
    }

    LOCK(cs);
    for (const auto& in : tx.vin) {
        auto otherIsLock = db.GetDirectSendLockByInput(in.prevout);
        if (!otherIsLock) {
            continue;
        }

        if (otherIsLock->txid != tx.GetHash()) {
            return otherIsLock;
        }
    }
    return nullptr;
}

size_t CDirectSendManager::GetDirectSendLockCount()
{
    return db.GetDirectSendLockCount();
}

void CDirectSendManager::WorkThreadMain()
{
    while (!workInterrupt) {
        bool didWork = false;

        didWork |= ProcessPendingDirectSendLocks();
        didWork |= ProcessPendingRetryLockTxs();

        if (!didWork) {
            if (!workInterrupt.sleep_for(std::chrono::milliseconds(100))) {
                return;
            }
        }
    }
}

bool IsOldDirectSendEnabled()
{
    return sporkManager.IsSporkActive(SPORK_2_DIRECTSEND_ENABLED) && !sporkManager.IsSporkActive(SPORK_20_DIRECTSEND_LLMQ_BASED);
}

bool IsNewDirectSendEnabled()
{
    return sporkManager.IsSporkActive(SPORK_2_DIRECTSEND_ENABLED) && sporkManager.IsSporkActive(SPORK_20_DIRECTSEND_LLMQ_BASED);
}

bool IsDirectSendEnabled()
{
    return sporkManager.IsSporkActive(SPORK_2_DIRECTSEND_ENABLED);
}

}
