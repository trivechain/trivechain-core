// Copyright (c) 2014-2017 The Dash Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef EXCLUSIVESENDCLIENT_H
#define EXCLUSIVESENDCLIENT_H

#include "masternode.h"
#include "exclusivesend.h"
#include "wallet/wallet.h"
#include "exclusivesend-util.h"

class CExclusiveSendClient;
class CConnman;

static const int DENOMS_COUNT_MAX                   = 100;

static const int DEFAULT_EXCLUSIVESEND_ROUNDS         = 2;
static const int DEFAULT_EXCLUSIVESEND_AMOUNT         = 1000;
static const int DEFAULT_EXCLUSIVESEND_LIQUIDITY      = 0;
static const bool DEFAULT_EXCLUSIVESEND_MULTISESSION  = false;

// Warn user if mixing in gui or try to create backup if mixing in daemon mode
// when we have only this many keys left
static const int EXCLUSIVESEND_KEYS_THRESHOLD_WARNING = 100;
// Stop mixing completely, it's too dangerous to continue when we have only this many keys left
static const int EXCLUSIVESEND_KEYS_THRESHOLD_STOP    = 50;

// The main object for accessing mixing
extern CExclusiveSendClient privateSendClient;

/** Used to keep track of current status of mixing pool
 */
class CExclusiveSendClient : public CExclusiveSendBase
{
private:
    mutable CCriticalSection cs_darksend;

    // Keep track of the used Masternodes
    std::vector<COutPoint> vecMasternodesUsed;

    std::vector<CAmount> vecDenominationsSkipped;
    std::vector<COutPoint> vecOutPointLocked;

    int nCachedLastSuccessBlock;
    int nMinBlocksToWait; // how many blocks to wait after one successful mixing tx in non-multisession mode

    // Keep track of current block height
    int nCachedBlockHeight;

    int nEntriesCount;
    bool fLastEntryAccepted;

    std::string strLastMessage;
    std::string strAutoDenomResult;

    CMutableTransaction txMyCollateral; // client side collateral

    CKeyHolderStorage keyHolderStorage; // storage for keys used in PrepareDenominate

    /// Check for process
    void CheckPool();
    void CompletedTransaction(PoolMessage nMessageID);

    bool IsDenomSkipped(CAmount nDenomValue) {
        return std::find(vecDenominationsSkipped.begin(), vecDenominationsSkipped.end(), nDenomValue) != vecDenominationsSkipped.end();
    }

    bool WaitForAnotherBlock();

    // Make sure we have enough keys since last backup
    bool CheckAutomaticBackup();
    bool JoinExistingQueue(CAmount nBalanceNeedsAnonymized, CConnman& connman);
    bool StartNewQueue(CAmount nValueMin, CAmount nBalanceNeedsAnonymized, CConnman& connman);

    /// Create denominations
    bool CreateDenominated(CConnman& connman);
    bool CreateDenominated(const CompactTallyItem& tallyItem, bool fCreateMixingCollaterals, CConnman& connman);

    /// Split up large inputs or make fee sized inputs
    bool MakeCollateralAmounts(CConnman& connman);
    bool MakeCollateralAmounts(const CompactTallyItem& tallyItem, bool fTryDenominated, CConnman& connman);

    /// As a client, submit part of a future mixing transaction to a Masternode to start the process
    bool SubmitDenominate(CConnman& connman);
    /// step 1: prepare denominated inputs and outputs
    bool PrepareDenominate(int nMinRounds, int nMaxRounds, std::string& strErrorRet, std::vector<CTxIn>& vecTxInRet, std::vector<CTxOut>& vecTxOutRet);
    /// step 2: send denominated inputs and outputs prepared in step 1
    bool SendDenominate(const std::vector<CTxIn>& vecTxIn, const std::vector<CTxOut>& vecTxOut, CConnman& connman);

    /// Get Masternode updates about the progress of mixing
    bool CheckPoolStateUpdate(PoolState nStateNew, int nEntriesCountNew, PoolStatusUpdate nStatusUpdate, PoolMessage nMessageID, int nSessionIDNew=0);
    // Set the 'state' value, with some logging and capturing when the state changed
    void SetState(PoolState nStateNew);

    /// As a client, check and sign the final transaction
    bool SignFinalTransaction(const CTransaction& finalTransactionNew, CNode* pnode, CConnman& connman);

    void RelayIn(const CDarkSendEntry& entry, CConnman& connman);

    void SetNull();

public:
    int nExclusiveSendRounds;
    int nExclusiveSendAmount;
    int nLiquidityProvider;
    bool fEnableExclusiveSend;
    bool fExclusiveSendMultiSession;

    masternode_info_t infoMixingMasternode;
    int nCachedNumBlocks; //used for the overview screen
    bool fCreateAutoBackups; //builtin support for automatic backups

    CExclusiveSendClient() :
        nCachedLastSuccessBlock(0),
        nMinBlocksToWait(1),
        txMyCollateral(CMutableTransaction()),
        nExclusiveSendRounds(DEFAULT_EXCLUSIVESEND_ROUNDS),
        nExclusiveSendAmount(DEFAULT_EXCLUSIVESEND_AMOUNT),
        nLiquidityProvider(DEFAULT_EXCLUSIVESEND_LIQUIDITY),
        fEnableExclusiveSend(false),
        fExclusiveSendMultiSession(DEFAULT_EXCLUSIVESEND_MULTISESSION),
        nCachedNumBlocks(std::numeric_limits<int>::max()),
        fCreateAutoBackups(true) { SetNull(); }

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, CConnman& connman);

    void ClearSkippedDenominations() { vecDenominationsSkipped.clear(); }

    void SetMinBlocksToWait(int nMinBlocksToWaitIn) { nMinBlocksToWait = nMinBlocksToWaitIn; }

    void ResetPool();

    void UnlockCoins();

    std::string GetStatus();

    /// Passively run mixing in the background according to the configuration in settings
    bool DoAutomaticDenominating(CConnman& connman, bool fDryRun=false);

    void CheckTimeout();

    /// Process a new block
    void NewBlock();

    void UpdatedBlockTip(const CBlockIndex *pindex);
};

void ThreadCheckExclusiveSendClient(CConnman& connman);

#endif
