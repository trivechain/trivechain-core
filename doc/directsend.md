## DirectSend Technical Information

DirectSend has been integrated into the Core Daemon in two ways:
* "push" notifications (ZMQ and `-directsendnotify` cmd-line/config option);
* RPC commands.

#### ZMQ

When a "Transaction Lock" occurs the hash of the related transaction is broadcasted through ZMQ using both the `zmqpubrawtxlock` and `zmqpubhashtxlock` channels.

* `zmqpubrawtxlock`: publishes the raw transaction when locked via DirectSend
* `zmqpubhashtxlock`: publishes the transaction hash when locked via DirectSend

This mechanism has been integrated into Bitcore-Node-Trivechain which allows for notification to be broadcast through Insight API in one of two ways:
* WebSocket: [https://github.com/trivechain/insight-api-trivechain#web-socket-api](https://github.com/trivechain/insight-api-trivechain#web-socket-api)
* API: [https://github.com/trivechain/insight-api-trivechain#directsend-transactions](https://github.com/trivechain/insight-api-trivechain#directsend-transactions)

#### Command line option

When a wallet DirectSend transaction is successfully locked a shell command provided in this option is executed (`%s` in `<cmd>` is replaced by TxID):

```
-directsendnotify=<cmd>
```

#### RPC

Details pertaining to an observed "Transaction Lock" can also be retrieved through RPC. There is a boolean field named `instantlock` which indicates whether a given transaction is locked via DirectSend. This field is present in the output of some wallet RPC commands e.g. `listsinceblock`, `gettransaction` etc. as well as in the output of some mempool RPC commands e.g. `getmempoolentry` and a couple of others like `getrawmempool` (for `verbose=true` only). For blockchain based RPC commands `instantlock` will also say `true` if this transaction was locked via LLMQ based ChainLocks (for backwards compatibility reasons).
