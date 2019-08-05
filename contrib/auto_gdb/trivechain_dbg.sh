#!/bin/bash
# use testnet settings,  if you need mainnet,  use ~/.trivechaincore/trivechaind.pid file instead
trivechain_pid=$(<~/.trivechaincore/testnet3/trivechaind.pid)
sudo gdb -batch -ex "source debug.gdb" trivechaind ${trivechain_pid}
