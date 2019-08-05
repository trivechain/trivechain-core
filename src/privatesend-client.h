// Copyright (c) 2014-2019 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef EXCLUSIVESENDCLIENT_H
#define EXCLUSIVESENDCLIENT_H

#include "exclusivesend-util.h"
#include "exclusivesend.h"
#include "wallet/wallet.h"

#include "evo/deterministicmns.h"

class CExclusiveSendClientManager;
class CConnman;
class CNode;


static const int MIN_EXCLUSIVESEND_SESSIONS = 1;
static const int MIN_EXCLUSIVESEND_ROUNDS = 2;
static const int MIN_EXCLUSIVESEND_AMOUNT = 2;
static const int MIN_EXCLUSIVESEND_DENOMS = 10;
static const int MIN_EXCLUSIVESEND_LIQUIDITY = 0;
static const int MAX_EXCLUSIVESEND_SESSIONS = 10;
static const int MAX_EXCLUSIVESEND_ROUNDS = 16;
static const int MAX_EXCLUSIVESEND_DENOMS = 100000;
static const int MAX_EXCLUSIVESEND_AMOUNT = MAX_MONEY / COIN;
static const int MAX_EXCLUSIVESEND_LIQUIDITY = 100;
static const int DEFAULT_EXCLUSIVESEND_SESSIONS = 4;
static const int DEFAULT_EXCLUSIVESEND_ROUNDS = 4;
static const int DEFAULT_EXCLUSIVESEND_AMOUNT = 1000;
static const int DEFAULT_EXCLUSIVESEND_DENOMS = 300;
static const int DEFAULT_EXCLUSIVESEND_LIQUIDITY = 0;

static const bool DEFAULT_EXCLUSIVESEND_MULTISESSION = false;

// Warn user if mixing in gui or try to create backup if mixing in daemon mode
// when we have only this many keys left
static const int EXCLUSIVESEND_KEYS_THRESHOLD_WARNING = 100;
// Stop mixing completely, it's too dangerous to continue when we have only this many keys left
static const int EXCLUSIVESEND_KEYS_THRESHOLD_STOP = 50;

// The main object for accessing mixing
extern CExclusiveSendClientManager exclusiveSendClient;

class CPendingDsaRequest
{
exclusive:
    static const int TIMEOUT = 15;

    CService addr;
    CExclusiveSendAccept dsa;
    int64_t nTimeCreated;

public:
    CPendingDsaRequest() :
        addr(CService()),
        dsa(CExclusiveSendAccept()),
        nTimeCreated(0)
    {
    }

    CPendingDsaRequest(const CService& addr_, const CExclusiveSendAccept& dsa_) :
        addr(addr_),
        dsa(dsa_),
        nTimeCreated(GetTime())
    {
    }

    CService GetAddr() { return addr; }
    CExclusiveSendAccept GetDSA() { return dsa; }
    bool IsExpired() { return GetTime() - nTimeCreated > TIMEOUT; }

    friend bool operator==(const CPendingDsaRequest& a, const CPendingDsaRequest& b)
    {
        return a.addr == b.addr && a.dsa == b.dsa;
    }
    friend bool operator!=(const CPendingDsaRequest& a, const CPendingDsaRequest& b)
    {
        return !(a == b);
    }
    explicit operator bool() const
    {
        return *this != CPendingDsaRequest();
    }
};

class CExclusiveSendClientSession : public CExclusiveSendBaseSession
{
exclusive:
    std::vector<COutPoint> vecOutPointLocked;

    int nEntriesCount;
    bool fLastEntryAccepted;

    std::string strLastMessage;
    std::string strAutoDenomResult;

    CDeterministicMNCPtr mixingMasternode;
    CMutableTransaction txMyCollateral; // client side collateral
    CPendingDsaRequest pendingDsaRequest;

    CKeyHolderStorage keyHolderStorage; // storage for keys used in PrepareDenominate

    /// Create denominations
    bool CreateDenominated(CAmount nBalanceToDenominate, CConnman& connman);
    bool CreateDenominated(CAmount nBalanceToDenominate, const CompactTallyItem& tallyItem, bool fCreateMixingCollaterals, CConnman& connman);

    /// Split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts(CConnman& connman);
    bool MakeCollateralAmounts(const CompactTallyItem& tallyItem, bool fTryDenominated, CConnman& connman);

    bool JoinExistingQueue(CAmount nBalanceNeedsAnonymized, CConnman& connman);
    bool StartNewQueue(CAmount nBalanceNeedsAnonymized, CConnman& connman);

    /// step 0: select denominated inputs and txouts
    bool SelectDenominate(std::string& strErrorRet, std::vector<std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsRet);
    /// step 1: prepare denominated inputs and outputs
    bool PrepareDenominate(int nMinRounds, int nMaxRounds, std::string& strErrorRet, const std::vector<std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsIn, std::vector<std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsRet, bool fDryRun = false);
    /// step 2: send denominated inputs and outputs prepared in step 1
    bool SendDenominate(const std::vector<std::pair<CTxDSIn, CTxOut> >& vecPSInOutPairsIn, CConnman& connman);

    /// Get Masternode updates about the progress of mixing
    bool CheckPoolStateUpdate(PoolState nStateNew, int nEntriesCountNew, PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID, int nSessionIDNew = 0);
    // Set the 'state' value, with some logging and capturing when the state changed
    void SetState(PoolState nStateNew);

    /// Check for process
    void CheckPool();
    void CompletedTransaction(PoolMessage nMessageID);

    /// As a client, check and sign the final transaction
    bool SignFinalTransaction(const CTransaction& finalTransactionNew, CNode* pnode, CConnman& connman);

    void RelayIn(const CExclusiveSendEntry& entry, CConnman& connman);

    void SetNull();

public:
    CExclusiveSendClientSession() :
        vecOutPointLocked(),
        nEntriesCount(0),
        fLastEntryAccepted(false),
        strLastMessage(),
        strAutoDenomResult(),
        mixingMasternode(),
        txMyCollateral(),
        pendingDsaRequest(),
        keyHolderStorage()
    {
    }

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    void UnlockCoins();

    void ResetPool();

    std::string GetStatus(bool fWaitForBlock);

    bool GetMixingMasternodeInfo(CDeterministicMNCPtr& ret) const;

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CConnman& connman, bool fDryRun = false);

    /// As a client, submit part of a future mixing transaction to a Masternode to start the process
    bool SubmitDenominate(CConnman& connman);

    bool ProcessPendingDsaRequest(CConnman& connman);

    bool CheckTimeout();
};

/** Used to keep track of current status of mixing pool
 */
class CExclusiveSendClientManager : public CExclusiveSendBaseManager
{
exclusive:
    // Keep track of the used Masternodes
    std::vector<COutPoint> vecMasternodesUsed;

    std::vector<CAmount> vecDenominationsSkipped;

    // TODO: or map<denom, CExclusiveSendClientSession> ??
    std::deque<CExclusiveSendClientSession> deqSessions;
    mutable CCriticalSection cs_deqsessions;

    int nCachedLastSuccessBlock;
    int nMinBlocksToWait; // how many blocks to wait after one successful mixing tx in non-multisession mode
    std::string strAutoDenomResult;

    // Keep track of current block height
    int nCachedBlockHeight;

    bool WaitForAnotherBlock();

    // Make sure we have enough keys since last backup
    bool CheckAutomaticBackup();

public:
    int nExclusiveSendSessions;
    int nExclusiveSendRounds;
    int nExclusiveSendAmount;
    int nExclusiveSendDenoms;
    int nLiquidityProvider;
    bool fEnableExclusiveSend;
    bool fExclusiveSendMultiSession;

    int nCachedNumBlocks;    //used for the overview screen
    bool fCreateAutoBackups; //builtin support for automatic backups

    CExclusiveSendClientManager() :
        vecMasternodesUsed(),
        vecDenominationsSkipped(),
        deqSessions(),
        nCachedLastSuccessBlock(0),
        nMinBlocksToWait(1),
        strAutoDenomResult(),
        nCachedBlockHeight(0),
        nExclusiveSendRounds(DEFAULT_EXCLUSIVESEND_ROUNDS),
        nExclusiveSendAmount(DEFAULT_EXCLUSIVESEND_AMOUNT),
        nExclusiveSendDenoms(DEFAULT_EXCLUSIVESEND_DENOMS),
        nLiquidityProvider(DEFAULT_EXCLUSIVESEND_LIQUIDITY),
        fEnableExclusiveSend(false),
        fExclusiveSendMultiSession(DEFAULT_EXCLUSIVESEND_MULTISESSION),
        nCachedNumBlocks(std::numeric_limits<int>::max()),
        fCreateAutoBackups(true)
    {
    }

    void ProcessMessage(CNode* pfrom, const std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    bool IsDenomSkipped(const CAmount& nDenomValue);
    void AddSkippedDenom(const CAmount& nDenomValue);
    void RemoveSkippedDenom(const CAmount& nDenomValue);

    void SetMinBlocksToWait(int nMinBlocksToWaitIn) { nMinBlocksToWait = nMinBlocksToWaitIn; }

    void ResetPool();

    std::string GetStatuses();
    std::string GetSessionDenoms();

    bool GetMixingMasternodesInfo(std::vector<CDeterministicMNCPtr>& vecDmnsRet) const;

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CConnman& connman, bool fDryRun = false);

    void CheckTimeout();

    void ProcessPendingDsaRequest(CConnman& connman);

    void AddUsedMasternode(const COutPoint& outpointMn);
    CDeterministicMNCPtr GetRandomNotUsedMasternode();

    void UpdatedSuccessBlock();

    void UpdatedBlockTip(const CBlockIndex* pindex);

    void DoMaintenance(CConnman& connman);
};

#endif
