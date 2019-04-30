
#TVIP Masternode Setup Guide ( For Windows QT Wallet )
=

## Prerequisites
- 10,000 TRVC
- A server ( Recommended: 4GB RAM, 2 cores CPU )
- Public IP with port 9999 opened from firewall
- TRVC latest QT Wallet ( https://github.com/trivechain/trivechain-core/releases )

#Steps
=

## 1. Prepare Trivechain-qt wallet
- Download the latest trivechain-qt wallet from https://github.com/trivechain/trivechain-core/releases
- Unzip the folder
- Fire up the trivechain-qt wallet
- Wait until sync complete
- Open the Debug console, from the main menu, select **Tools > Debug console**

## 2. Get masternode private key
- Execute command `masternode genkey` in **Debug console**
- Mark down your masternode private key

## 3. Get masternode address
- Execute command `getaccountaddress mymasternode` in **Debug console**
- Mark down the masternode address

## 4. Deposit
- Send 10,000 TRVC to the masternode address
- Wait for 6 confirmations
- Note: Make sure the "Substract fee from amount" is **unchecked**
- Note: Send exactly 10,000 TRVC, any other amount is not recognized

## 5. Get the transaction details
- Execute command `masternode outputs` in **Debug console**
- You will receive an array of valid transaction pairs of "transaction id" : "sequence"
- Mark down the transaction id and sequence

## 6. Edit masternode configuration file
- From the main menu, select **Tools > Open Masternode Configuration File**
- Add a line as shown by the example, replace with your details retrieved from **Step 2 & 5**
```
mn1 127.0.0.2:9999 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
```
Explanation:

| Name | Masternode IP Address | Port | Masternode private key | Transaction ID | Transaction Sequence |
|--|--|--|--|--|--|
| mn1 | 127.0.0.2 | 9999 | 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg | 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c | 0 |

- Save and close the file

## 7. Edit Trivechain daemon configuration file
- From the main menu, select **Tools > Open Wallet Configuration File**
```
rpcuser=XXXXXXXXXXXXX  
rpcpassword=XXXXXXXXXXXXXXXXXXXXXXXXXXXX  
rpcallowip=127.0.0.1  
listen=1  
server=1  
daemon=1  
maxconnections=24  
masternode=1  
masternodeprivkey=XXXXXXXXXXXXXXXXXXXXXXX  
externalip=XXX.XXX.XXX.XXX
```
- Replace the XXXXX:
	- masternodeprivkey : key from **Step 2**
	- externalip : Your server public IP
- Save and close the file

## 8. Enable the masternode tab
- From the main menu, select **Settings > Options > Wallet**
- Check **Show Masternodes Tab**, click **OK**
- Close Trivechain-qt wallet

## 9. Start masternode
- Start Trivechain-qt wallet
- Go to Masternodes Tab
- Click **Start all**
- Note: The status will be PRE_ENABLED, after few minutes, you will see the status change to ENABLED

# That's it!
Congratulations in setting up your first masternode! Feel free to discuss in https://trivechain.com if you need further assistance
