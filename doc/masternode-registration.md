
TRVC Masternode Setup Guide ( Windows QT Wallet with Ubuntu Server )
=

## Prerequisites
- 10,000 TRVC
- TRVC latest QT Wallet on Windows Desktop ( https://github.com/trivechain/trivechain-core/releases )
- A Ubuntu Server ( Recommended: 2GB RAM, 2 cores vCPU )
  - Public IP with port 9999 opened from firewall
  - TRVC latest Linux Daemon on Ubuntu Server( https://github.com/trivechain/trivechain-core/releases )
  - TRVC latest Sentinel on Ubuntu Server( https://github.com/trivechain/sentinel )

Steps
=

## 1. Prepare Trivechain-qt wallet
- Download the latest trivechain-qt wallet from https://github.com/trivechain/trivechain-core/releases
- Unzip the folder
- Fire up the trivechain-qt wallet
- Wait until sync complete
- Open the Debug console, from the main menu, select **Tools > Debug console**

## 2. Get masternode address
- Execute command `getaccountaddress mymasternode` in **Debug console**
- Mark down the masternode address

## 3. Deposit to masternode address
- Send 10,000 TRVC to the masternode address
- Wait for 15 confirmations
- Note: Make sure the "Substract fee from amount" is **unchecked**
- Note: Send exactly 10,000 TRVC, any other amount is not recognized

## 4. Get an address to pay protx fee
- Execute command `getaccountaddress feeSourceAddress` in **Debug console**
- Mark down the fee source address

## 5. Deposit to fee source address
- Send 0.0001 TRVC to the fee source address
- Wait for 15 confirmations
- Note: you can send any amount as long as enough to cover transaction fee or use an address that already have funds

## 6. Get voting/owner address
- Execute command `getaccountaddress myvotingaddress` in **Debug console**
- Mark down the voting address

## 7. Get the transaction details
- Execute command `masternode outputs` in **Debug console**
- You will receive an array of valid transaction pairs of "collateralHash" : "collateralIndex"
- Mark down the collateralHash and collateralIndex

## 8. Prepare a ProRegTx transaction
We will now prepare an unsigned ProRegTx special transaction using the `protx register_prepare` command in **Debug console**. This command has the following syntax:
```
protx register_prepare collateralHash collateralIndex ipAndPort ownerKeyAddr
  operatorPubKey votingKeyAddr operatorReward payoutAddress feeSourceAddress
```

- `collateralHash`: The txid of the 10,000 TRVC collateral funding transaction
- `collateralIndex`: The output index of the 10,000 TRVC funding transaction
- `ipAndPort`: Masternode IP address and port, in the format x.x.x.x:yyyy (or provided by your hosting service)
- `ownerKeyAddr`: The new TRVC address generated above for the owner/voting address (myvotingaddress address)
- `operatorPubKey`: The BLS public key generated above (or provided by your hosting service)
- `votingKeyAddr`: The new TRVC address generated above, or the address of a delegate, used for proposal voting (myvotingaddress address)
- `operatorReward`: The percentage of the block reward allocated to the operator as payment in percentage ( 1 = 1% )
- `payoutAddress`: A new or existing TRVC address to receive the owner’s masternode rewards (any address)
- `feeSourceAddress`: An address used to fund ProTx fee.

Example (remove line breaks if copying):
```
protx register_prepare
  2c499e3862e5aa5f220278f42f9dfac32566d50f1e70ae0585dd13290227fdc7
  1
  140.82.59.51:9999
  yc98KR6YQRo1qZVBhp2ZwuiNM7hcrMfGfz
  01d2c43f022eeceaaf09532d84350feb49d7e72c183e56737c816076d0e803d4f86036bd4151160f5732ab4a461bd127
  yc98KR6YQRo1qZVBhp2ZwuiNM7hcrMfGfz
  0
  ycBFJGv7V95aSs6XvMewFyp1AMngeRHBwy
```
Output:
```
{
  "tx": "030001000191def1f8bb265861f92e9984ac25c5142ebeda44901334e304c447dad5adf6070000000000feffffff0121dff505000000001976a9149e2deda2452b57e999685cb7dabdd6f4c3937f0788ac00000000d1010000000000c7fd27022913dd8505ae701e0fd56625c3fa9d2ff47802225faae562389e492c0100000000000000000000000000ffff8c523b334e1fad8e6259e14db7d05431ef4333d94b70df1391c601d2c43f022eeceaaf09532d84350feb49d7e72c183e56737c816076d0e803d4f86036bd4151160f5732ab4a461bd127ad8e6259e14db7d05431ef4333d94b70df1391c600001976a914adf50b01774202a184a2c7150593442b89c212e788acf8d42b331ae7a29076b464e61fdbcfc0b13f611d3d7f88bbe066e6ebabdfab7700",
  "collateralAddress": "yPd75LrstM268Sr4hD7RfQe5SHtn9UMSEG",
  "signMessage": "ycBFJGv7V95aSs6XvMewFyp1AMngeRHBwy|0|yc98KR6YQRo1qZVBhp2ZwuiNM7hcrMfGfz|yc98KR6YQRo1qZVBhp2ZwuiNM7hcrMfGfz|54e34b8b996839c32f91e28a9e5806ec5ba5a1dadcffe47719f5b808219acf84"
}
```
Next we will use the `collateralAddress` and `signMessage` fields to sign the transaction, and the output of the `tx` field to submit the transaction.

## 9. Sign the ProRegTx transaction
Now we will sign the content of the `signMessage` field using the private key for the collateral address as specified in `collateralAddress`. Note that no internet connection is required for this step, meaning that the wallet can remain disconnected from the internet in cold storage to sign the message. In this example we will again use Trivechain, but it is equally possible to use the signing function of a hardware wallet. The command takes the following syntax:
```
signmessage collateralAddress signMessage
```
Example:
```
signmessage yPd75LrstM268Sr4hD7RfQe5SHtn9UMSEG "ycBFJGv7V95aSs6XvMewFyp1AMngeRHBwy|0|yc98KR6YQRo1qZVBhp2ZwuiNM7hcrMfGfz|yc98KR6YQRo1qZVBhp2ZwuiNM7hcrMfGfz|54e34b8b996839c32f91e28a9e5806ec5ba5a1dadcffe47719f5b808219acf84"
```
Output:
```
IMf5P6WT60E+QcA5+ixors38umHuhTxx6TNHMsf9gLTIPcpilXkm1jDglMpK+JND0W3k/Z+NzEWUxvRy71NEDns=
```

## 10. Submit the signed message
```
protx register_submit tx sig
```
Where:
```
tx: The serialized transaction previously returned in the tx output field from the protx register_prepare command
sig: The message signed with the collateral key from the signmessage command
```
Example:
```
protx register_submit 030001000191def1f8bb265861f92e9984ac25c5142ebeda44901334e304c447dad5adf6070000000000feffffff0121dff505000000001976a9149e2deda2452b57e999685cb7dabdd6f4c3937f0788ac00000000d1010000000000c7fd27022913dd8505ae701e0fd56625c3fa9d2ff47802225faae562389e492c0100000000000000000000000000ffff8c523b334e1fad8e6259e14db7d05431ef4333d94b70df1391c601d2c43f022eeceaaf09532d84350feb49d7e72c183e56737c816076d0e803d4f86036bd4151160f5732ab4a461bd127ad8e6259e14db7d05431ef4333d94b70df1391c600001976a914adf50b01774202a184a2c7150593442b89c212e788acf8d42b331ae7a29076b464e61fdbcfc0b13f611d3d7f88bbe066e6ebabdfab7700 "IMf5P6WT60E+QcA5+ixors38umHuhTxx6TNHMsf9gLTIPcpilXkm1jDglMpK+JND0W3k/Z+NzEWUxvRy71NEDns="
```
Output:
```
9f5ec7540baeefc4b7581d88d236792851f26b4b754684a31ee35d09bdfb7fb6
```

## 11. Enable the masternode tab
- From the main menu, select **Settings > Options > Wallet**
- Check **Show Masternodes Tab**, click **OK**
- Close Trivechain-qt wallet
- Start Trivechain-qt wallet
- Go to Masternodes Tab
- You should be able to see your Masternode details
- Note: The status will be PRE_ENABLED, after few minutes, you will see the status change to ENABLED

# That's it!
Congratulations in setting up your first masternode! Feel free to discuss in https://trivechain.com if you need further assistance
