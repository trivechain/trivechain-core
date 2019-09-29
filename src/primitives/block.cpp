// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "streams.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "crypto/common.h"
#include "algo/hash_algos.h"


static const uint32_t X16R_ACTIVATION_TIME = 1555872222;
static const uint32_t MAINNET_X16RV2_ACTIVATION_TIME = 1569945600;
static const uint32_t TESTNET_X16RV2_ACTIVATION_TIME = 1569152000;
static const uint32_t REGTEST_X16RV2_ACTIVATION_TIME = 1569152000;

BlockNetwork bNetwork = BlockNetwork();

BlockNetwork::BlockNetwork()
{
    fOnTestnet = false;
    fOnRegtest = false;
}

void BlockNetwork::SetNetwork(const std::string& net)
{
    if (net == "test") {
        fOnTestnet = true;
    } else if (net == "regtest") {
        fOnRegtest = true;
    }
}

uint256 CBlockHeader::GetHash() const
{
    if (bNetwork.fOnTestnet) {
        if (nTime > TESTNET_X16RV2_ACTIVATION_TIME) {
            return HashX16RV2(BEGIN(nVersion), END(nNonce), hashPrevBlock);
        } 
    } else if (bNetwork.fOnRegtest) {
        if (nTime > REGTEST_X16RV2_ACTIVATION_TIME) {
            return HashX16RV2(BEGIN(nVersion), END(nNonce), hashPrevBlock);
        }
    }
    if (nTime > MAINNET_X16RV2_ACTIVATION_TIME) {
        return HashX16RV2(BEGIN(nVersion), END(nNonce), hashPrevBlock);
    } else if (nTime > X16R_ACTIVATION_TIME) {
        return HashX16R(BEGIN(nVersion), END(nNonce), hashPrevBlock);
    }
    return HashX11(BEGIN(nVersion), END(nNonce));
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i]->ToString() << "\n";
    }
    return s.str();
}
