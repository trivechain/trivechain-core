
#TVIP Masternode Setup Guide
=
Each TVIP masternode will require a deposit of 1000TRVC, the masternode is valid as long as deposit is untouched. This setup will require 2 separate machines, a controller and a masternode.

## Prerequisites
- 1000TRVC
- A server with dedicated IP, at least 4GB RAM and 2 cores
- A Trivecoin-qt wallet ( main controller )
 
 
#Steps
=

## 1.0 Prepare masternode private key
- Execute command `masternode genkey`
- Mark down your masternode private key

## 1.1 Prepare masternode address
- Fire up your Trivecoin-qt wallet
- From the main menu, select **Tools > Debug console**
- Execute command `getaccountaddress mymasternode`
- Mark down the address

## 1.2 Deposit
- Send 1000TRVC to the masternode address
- Make sure the "Substract fee from amount" is **unchecked**
- Wait for 6 confirmations

## 1.3 Get the transaction details
- Return to the debug console
- Execute command `masternode outputs`
- You will receive an array of valid transaction pairs of
	- "transaction id" : "sequence"


## 1.4 Edit masternode configuration file
- From the Trivecoin-qt wallet main menu, select **Tools > Open Masternode Configuration File**
- Add a line as shown by the example, replace with your details retrieved from step **1.0** & **1.3**
```
mn1 127.0.0.2:9999 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0
```
Explanation:
| Name | Masternode IP Address | Port | Masternode private key | Transaction ID | Transaction Index |
|--|--|--|--|--|--|
| mn1 | 127.0.0.2 | 9999 | 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg | 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c | 0 |

- Save and quit the Trivecoin-qt wallet for now

## 2.0 Masternode server environment setup
- Make sure the masternode server firewall allow incoming port 22 and 9999

## 2.1 Trivecoin daemon configuration
- Create a file **~/.trivecoin/trivecoin.conf** 
	* Windows: %APPDATA%\\TriveCoin\trivecoin.conf
	* Mac OS: ~/Library/Application Support/TriveCoin/trivecoin.conf
	* Unix/Linux: ~/.trivecoin/trivecoin.conf
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
	- masternodeprivkey : key from step **1.0**
	- externalip : Your masternode server IP
- Run the `trivecoind`

## 3.0 Start masternode
- Open your Trivecoin-qt wallet again
- From the **trivecoin-qt** menu, select **Preferences**
- Go to **Wallet** tab
- Check **Show Masternodes Tab**, click OK
- Go to Masternodes Tab
- Click **Start All**

# That's it!
Congratulations in setting up your first masternode! Feel free to discuss in https://trivecoin.org if you need further assistance