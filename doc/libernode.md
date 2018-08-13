Libernode Build Instructions and Notes
=============================
 - Version 0.1.0
 - Date: 8 March 2018

Prerequisites
-------------
 - Ubuntu 16.04+
 - Libraries to build from libercoin source
 - Port **8255** is open

Step 1. Build
----------------------
**1.1.**  Check out from source:

    git clone https://github.com/libercoinofficial/libercoin

**1.2.**  See [README.md](README.md) for instructions on building.

Step 2. (Optional - only if firewall is running). Open port 8255
----------------------
**2.1.**  Run:

    sudo ufw allow 8255
    sudo ufw default allow outgoing
    sudo ufw enable

Step 3. First run on your Local Wallet
----------------------
**3.0.**  Go to the checked out folder

    cd libercoin

**3.1.**  Start daemon in testnet mode:

    ./src/libercoin_daemon -daemon -server -testnet

**3.2.**  Generate libernodeprivkey:

    ./src/libercoin-cli libernode genkey

(Store this key)

**3.3.**  Get wallet address:

    ./src/libercoin-cli getaccountaddress 0

**3.4.**  Send to received address **exactly 25000 ZOI** in **1 transaction**. Wait for 6 confirmations.

**3.5.**  Stop daemon:

    ./src/libercoin-cli stop

Step 4. In your VPS where you are hosting your Libernode. Update config files
----------------------
**4.1.**  Create file **libercoin.conf** (in folder **~/.libercoin**)

    rpcuser=username
    rpcpassword=password
    rpcallowip=127.0.0.1
    debug=1
    txindex=1
    daemon=1
    server=1
    listen=1
    maxconnections=24
    libernode=1
    libernodeprivkey=XXXXXXXXXXXXXXXXX  ## Replace with your libernode private key
    externalip=XXX.XXX.XXX.XXX:8255 ## Replace with your node external IP

**4.2.**  Create file **zonode.conf** (in 2 folders **~/.libercoin** and **~/.libercoin/testnet3**) contains the following info:
 - LABEL: A one word name you make up to call your node (ex. ZN1)
 - IP:PORT: Your libernode VPS's IP, and the port is always 8255.
 - LIBERNODEPRIVKEY: This is the result of your "libernode genkey" from earlier.
 - TRANSACTION HASH: The collateral tx. hash from the 25000 ZOI deposit.
 - INDEX: The Index is always 0 or 1.

To get TRANSACTION HASH, run:

    ./src/libercoin-cli libernode outputs

The output will look like:

    { "d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb" : "0", }

Sample of libernode.conf:

    ZN1 51.52.53.54:18168 XrxSr3fXpX3dZcU7CoiFuFWqeHYw83r28btCFfIHqf6zkMp1PZ4 d6fd38868bb8f9958e34d5155437d009b72dfd33fc28874c87fd42e51c0f74fdb 0

Step 5. Run a libernode
----------------------
**5.1.**  Start libernode:

    ./src/libercoin-cli libernode start-alias <LABEL>

For example:

    ./src/libercoin-cli libernode start-alias ZN1

**5.2.**  To check node status:

    ./src/libercoin-cli libernode debug

If not successfully started, just repeat start command
