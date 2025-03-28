Esher Core
======================

[Esher Core](https://github.com/esherproject/esher-core) is the Esher blockchain node software and command-line wallet software.
For UI reference wallet software (browser-based wallet and desktop wallet) visit [Esher UI](https://github.com/esherproject/esher-ui).

Visit [esherproject.github.io](https://esherproject.github.io/) to learn about Esher and join the community at [Esher Discord Server](https://discord.gg/zsh2PZt).

Information for developers can be found in the [Wiki](https://github.com/esherproject/esher-core/wiki) and the [Esher Developer Portal](https://dev.eshercrypto.com/). Users interested in how Esher works can go to the [Esher Documentation](https://how.eshercrypto.com/) site.

Visit [Awesome Esher](https://github.com/esherproject/awesome-esher) to find more resources and links E.G. chat groups, client libraries and extended APIs.

* [Getting Started](#getting-started)
* [Support](#support)
* [Using Built-In APIs](#using-built-in-apis)
* [Accessing restrictable node API sets](#accessing-restrictable-node-api-sets)
* [FAQ](#faq)
* [License](#license)

Getting Started
---------------

Build instructions and additional documentation are available in the
[Wiki](https://github.com/esherproject/esher-core/wiki).

Prebuilt binaries can be found in the [releases page](https://github.com/esherproject/esher-core/releases) for download.


### Installing Node and Command-Line Wallet Software

We recommend building on Ubuntu 20.04 LTS (64-bit)

**Install Operating System Dependencies:**

    sudo apt-get update
    sudo apt-get install autoconf cmake make automake libtool git libboost-all-dev libssl-dev g++ libcurl4-openssl-dev doxygen

**Build Node And Command-Line Wallet:**

    git clone https://github.com/esherproject/esher-core.git
    cd esher-core
    git checkout master # may substitute "master" with current release tag
    git submodule update --init --recursive
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make

**Upgrade Node And Command-Line Wallet:**

    cd esher-core
    git remote set-url origin https://github.com/esherproject/esher-core.git
    git checkout master
    git remote set-head origin --auto
    git pull
    git submodule update --init --recursive # this command may fail
    git submodule sync --recursive
    git submodule update --init --recursive
    mkdir build
    cd build
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make

**NOTE:**

* Esher requires a 64-bit operating system to build, and will not build on a 32-bit OS. Tested operating systems:
  * Linux (heavily tested with Ubuntu LTS releases)
  * macOS (various versions)
  * Windows (various versions, Visual Studio and MinGW)
  * OpenBSD (various versions)

* Esher requires [Boost](https://www.boost.org/) libraries to build, supports version `1.58` to `1.74`.
Newer versions may work, but have not been tested.
If your system came pre-installed with a version of Boost libraries that you do not wish to use, you may
manually build your preferred version and use it with Esher by specifying it on the CMake command line.

  Example: `cmake -DBOOST_ROOT=/path/to/boost ..`

* Esher requires [OpenSSL](https://www.openssl.org/) libraries to build, supports version `1.0.2` to `1.1.1`.
If your system came pre-installed with a version of OpenSSL libraries that you do not wish to use, you may
manually build your preferred version and use it with Esher by specifying it on the CMake command line.

  Example: `cmake -DOPENSSL_ROOT_DIR=/path/to/openssl ..`


### Running and Stopping Node Software

**Run Node Software:**

Stay on `esher-core/build` directory before you run the below `witness_node` command

    ./programs/witness_node/witness_node

Under `build` directory the node run will automatically create the directory `witness_node_data_dir` along with config files underneath then start synchronizing the blockchain.
It may take usually several hours to fully synchronize the blockchain data.
The blockchain data will be stored under the directory `witness_node_data_dir`.

**Stop Node Software:**

For stopping the node run cleanly, you will need to access the node run terminal then press on `Ctrl+C` then wait for the run to stop, please note that it may take usually few minutes to exit the run.
It's recommended to use linux command [screen](https://help.ubuntu.com/community/Screen) to initiate the node run so you can go back to the node run screen to stop it.


**IMPORTANT:** By default the node will start in reduced memory mode by using some of the commands detailed in [Memory reduction for nodes](https://github.com/esherproject/esher-core/wiki/Memory-reduction-for-nodes).
In order to run a full node with all the account histories which usually unnecessary, you need to remove `partial-operations` and `max-ops-per-account` from your config file.

To use the command-line wallet or other wallets / clients with the node, the node need to be started with RPC connection enabled, which can be done by starting the node with the `--rpc-endpoint` parameter, E.G.

    ./programs/witness_node/witness_node --rpc-endpoint=127.0.0.1:8090

or configure it in the config file by editing `witness_node_data_dir/config.ini` as follows:

    rpc-endpoint = 127.0.0.1:8090

You can run the program with `--help` parameter to see more info:

    ./programs/witness_node/witness_node --help


### Using Command-Line Wallet

Stay on `esher-core/build` directory before you run the below `cli_wallet` command

    ./programs/cli_wallet/cli_wallet

**IMPORTANT:** The `cli_wallet` or API interfaces to the node wouldn't be fully functional unless the node is fully synchronized with the blockchain. The `cli_wallet` command `info` will show result `head_block_age` which will tell you how far you are from the live current block of the blockchain.

To check your current block:

    new >>> info

To query the blockchain, E.G. get info about an account:

    new >>> get_account <account_name_or_id>

If you need to transact with your account but not only query, firstly set your initial password and unlock the wallet:

* For non-Windows operating systems, you can type the commands and press `[ENTER]`, then input the password and press `[ENTER]`, in this case the password won't show:

      new >>> set_password [ENTER]
      Enter password:
      locked >>> unlock [ENTER]
      Enter password:
      unlocked >>>

* For Windows, or you'd like to show the password, type the commands with the password:

      new >>> set_password <PASSWORD>
      locked >>> unlock <PASSWORD>
      unlocked >>>

To be able to transact with your account, import the corresponding private keys:

    unlocked >>> import_key <ACCOUNT_NAME> <WIF_KEY>

The private keys will be encrypted and stored in the wallet file, the file name is `wallet.json` by default.
The private keys are accessible when the wallet is unlocked.

    unlocked >>> dump_private_keys

Use `lock` command to make the private keys inaccessible. There is no auto-lock feature so far.

    unlocked >>> lock

To import your initial (genesis) balances, import the private keys corresponding to the balances:

    unlocked >>> import_balance <ACCOUNT_NAME> [<WIF_KEY> ...] true

Use `help` to see all available wallet commands.

    >>> help

Use `gethelp <COMMAND>` to see more info about individual commands. E.G.

    >>> gethelp get_order_book

The definition of all commands is available in the
[wallet.hpp](https://github.com/esherproject/esher-core/blob/master/libraries/wallet/include/graphene/wallet/wallet.hpp) source code file.
Corresponding documentation can be found in the [Doxygen documentation](https://esherproject.github.io/doxygen/classgraphene_1_1wallet_1_1wallet__api.html).

You can run the program with `--help` parameter to see more info:

    ./programs/cli_wallet/cli_wallet --help

There is also some info in the [Wiki](https://github.com/esherproject/esher-core/wiki/CLI-Wallet-Cookbook).


Using Built-In APIs
-------------

### Node API

The `witness_node` software provides several different API sets, known as *node API*.

Each API set has its own ID and a name.
When running `witness_node` with RPC connection enabled, initially two API sets are available:
* API set with ID `0` has name *"database"*, it provides read-only access to the database,
* API set with ID `1` has name *"login"*, it is used to login and gain access to additional, restrictable API sets.

Here is an example using `wscat` package from `npm` for websockets:

    $ npm install -g wscat
    $ wscat -c ws://127.0.0.1:8090
    > {"id":1, "method":"call", "params":[0,"get_accounts",[["1.2.0"]]]}
    < {"id":1,"result":[{"id":"1.2.0","annotations":[],"membership_expiration_date":"1969-12-31T23:59:59","registrar":"1.2.0","referrer":"1.2.0","lifetime_referrer":"1.2.0","network_fee_percentage":2000,"lifetime_referrer_fee_percentage":8000,"referrer_rewards_percentage":0,"name":"committee-account","owner":{"weight_threshold":1,"account_auths":[],"key_auths":[],"address_auths":[]},"active":{"weight_threshold":6,"account_auths":[["1.2.5",1],["1.2.6",1],["1.2.7",1],["1.2.8",1],["1.2.9",1],["1.2.10",1],["1.2.11",1],["1.2.12",1],["1.2.13",1],["1.2.14",1]],"key_auths":[],"address_auths":[]},"options":{"memo_key":"GPH1111111111111111111111111111111114T1Anm","voting_account":"1.2.0","num_witness":0,"num_committee":0,"votes":[],"extensions":[]},"statistics":"2.7.0","whitelisting_accounts":[],"blacklisting_accounts":[]}]}

We can do the same thing using an HTTP client such as `curl` for APIs which do not require login or other session state:

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": [0, "get_accounts", [["1.2.0"]]], "id": 1}' http://127.0.0.1:8090/
    {"id":1,"result":[{"id":"1.2.0","annotations":[],"membership_expiration_date":"1969-12-31T23:59:59","registrar":"1.2.0","referrer":"1.2.0","lifetime_referrer":"1.2.0","network_fee_percentage":2000,"lifetime_referrer_fee_percentage":8000,"referrer_rewards_percentage":0,"name":"committee-account","owner":{"weight_threshold":1,"account_auths":[],"key_auths":[],"address_auths":[]},"active":{"weight_threshold":6,"account_auths":[["1.2.5",1],["1.2.6",1],["1.2.7",1],["1.2.8",1],["1.2.9",1],["1.2.10",1],["1.2.11",1],["1.2.12",1],["1.2.13",1],["1.2.14",1]],"key_auths":[],"address_auths":[]},"options":{"memo_key":"GPH1111111111111111111111111111111114T1Anm","voting_account":"1.2.0","num_witness":0,"num_committee":0,"votes":[],"extensions":[]},"statistics":"2.7.0","whitelisting_accounts":[],"blacklisting_accounts":[]}]}

When using an HTTP client, the API set ID can be replaced by the API set name, E.G.

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": ["database", "get_accounts", [["1.2.0"]]], "id": 1}' http://127.0.0.1:8090/

The definition of all node APIs is available in the source code files including
[database_api.hpp](https://github.com/esherproject/esher-core/blob/master/libraries/app/include/graphene/app/database_api.hpp)
and [api.hpp](https://github.com/esherproject/esher-core/blob/master/libraries/app/include/graphene/app/api.hpp).
Corresponding documentation can be found in Doxygen:
* [database API](https://esherproject.github.io/doxygen/classgraphene_1_1app_1_1database__api.html)
* [other APIs](https://esherproject.github.io/doxygen/namespacegraphene_1_1app.html)


### Wallet API

The `cli_wallet` program can also be configured to serve **all of its commands** as APIs, known as *wallet API*.

Start `cli_wallet` with RPC connection enabled:

    $ ./programs/cli_wallet/cli_wallet --rpc-http-endpoint=127.0.0.1:8093

Access the wallet API using an HTTP client:

    $ curl --data '{"jsonrpc": "2.0", "method": "info", "params": [], "id": 1}' http://127.0.0.1:8093/
    $ curl --data '{"jsonrpc": "2.0", "method": "get_account", "params": ["1.2.0"], "id": 1}' http://127.0.0.1:8093/

Note: The syntax to access wallet API is a bit different than accessing node API.

**Important:**
* When RPC connection is enabled for `cli_wallet`, sensitive data E.G. private keys which is accessible via commands will be accessible via RPC too. It is recommended that only open network connection to localhost or trusted addresses E.G. configure a firewall.
* When using wallet API, sensitive data E.G. the wallet password and private keys is transmitted as plain text, thus may be vulnerable to network sniffing. It is recommended that only use wallet API with localhost, or in a clean network, and / or use `--rpc-tls-endpoint` parameter to only serve wallet API via secure connections.


Accessing restrictable node API sets
------------------------------------

You can restrict node API sets to particular users by specifying an `api-access` file in `config.ini`
or by using the `--api-access /full/path/to/api-access.json` command-line option on node startup. Here is an example `api-access` file which allows
user `bytemaster` with password `supersecret` to access four different API sets, while allowing any other user to access the three public API sets
necessary to use the node:

    {
       "permission_map" :
       [
          [
             "bytemaster",
             {
                "password_hash_b64" : "9e9GF7ooXVb9k4BoSfNIPTelXeGOZ5DrgOYMj94elaY=",
                "password_salt_b64" : "INDdM6iCi/8=",
                "allowed_apis" : ["database_api", "network_broadcast_api", "history_api", "network_node_api"]
             }
          ],
          [
             "*",
             {
                "password_hash_b64" : "*",
                "password_salt_b64" : "*",
                "allowed_apis" : ["database_api", "network_broadcast_api", "history_api"]
             }
          ]
       ]
    }

Note: the `login` API set is always accessible.

Passwords are stored in `base64` as salted `sha256` hashes.  A simple Python script,
[`saltpass.py`](https://github.com/esherproject/esher-core/blob/master/programs/witness_node/saltpass.py)
is available to obtain hash and salt values from a password.
A single asterisk `"*"` may be specified as username or password hash to accept any value.

With the above configuration, here is an example of how to call the `add_node` API from the `network_node` API set:

    {"id":1, "method":"call", "params":[1,"login",["bytemaster", "supersecret"]]}
    {"id":2, "method":"call", "params":[1,"network_node",[]]}
    {"id":3, "method":"call", "params":[2,"add_node",["127.0.0.1:9090"]]}

Note, the call to `network_node` is necessary to obtain the correct API set ID for the `network_node` API set.  It is not guaranteed that the API set ID for the `network_node` API set will always be `2`.

The restricted API sets are accessible via HTTP too using *basic access authentication*. E.G.

    $ curl --data '{"jsonrpc": "2.0", "method": "call", "params": ["network_node", "add_node", ["127.0.0.1:9090"]], "id": 1}' http://bytemaster:supersecret@127.0.0.1:8090/

Our `doxygen` documentation contains the most up-to-date information
about APIs for the [node](https://esherproject.github.io/doxygen/namespacegraphene_1_1app.html) and the
[wallet](https://esherproject.github.io/doxygen/classgraphene_1_1wallet_1_1wallet__api.html).


FAQ
---

- Is there a way to generate help with parameter names and method descriptions?

    Yes. Documentation of the code base, including APIs, can be generated using Doxygen. Simply run `doxygen` in this directory.

    If both Doxygen and perl are available in your build environment, the command-line wallet's `help` and `gethelp`
    commands will display help generated from the doxygen documentation.

    If your command-line wallet's `help` command displays descriptions without parameter names like
        `signed_transaction transfer(string, string, string, string, string, bool)`
    it means CMake was unable to find Doxygen or perl during configuration.  If found, the
    output should look like this:
        `signed_transaction transfer(string from, string to, string amount, string asset_symbol, string memo, bool broadcast)`

- Is there a way to allow external program to drive `cli_wallet` via websocket, JSONRPC, or HTTP?

    Yes. External programs may connect to the command-line wallet and make its calls over a websockets API. To do this, run the wallet in
    server mode, i.e. `cli_wallet -H "127.0.0.1:9999"` and then have the external program connect to it over the specified port
    (in this example, port 9999). Please check the ["Using Built-In APIs"](#using-built-in-apis) section for more info.

- Is there a way to access methods which require login over HTTP?

    Yes. Most of the methods can be accessed by specifying the API name instead of an API ID. If an API is protected by a username and a password, it can be accessed by using *basic access authentication*. Please check the ["Accessing restrictable node API sets"](#accessing-restrictable-node-api-sets) section for more info.

    However, HTTP is not really designed for "server push" notifications, and we would have to figure out a way to queue notifications for a polling client. Websockets solves this problem. If you need to access the stateful methods, use Websockets.

- What is the meaning of `a.b.c` numbers?

    The first number specifies the *space*.  Space `1` is for protocol objects, `2` is for implementation objects.
    Protocol space objects can appear on the wire, for example in the binary form of transactions.
    Implementation space objects cannot appear on the wire and solely exist for implementation
    purposes, such as optimization or internal bookkeeping.

    The second number specifies the *type*.  The type of the object determines what fields it has.  For a
    complete list of type IDs, see `GRAPHENE_DEFINE_IDS(protocol, protocol_ids ...)` in
    [protocol/types.hpp](https://github.com/esherproject/esher-core/blob/master/libraries/protocol/include/graphene/protocol/types.hpp)
    and `GRAPHENE_DEFINE_IDS(chain, implementation_ids ...)` in [chain/types.hpp](https://github.com/esherproject/esher-core/blob/master/libraries/chain/include/graphene/chain/types.hpp).

    The third number specifies the *instance*.  The instance of the object is different for each individual
    object.

- The answer to the previous question was really confusing.  Can you make it clearer?

    All account IDs are of the form `1.2.x`.  If you were the 9735th account to be registered,
    your account's ID will be `1.2.9735`.  Account `0` is special (it's the "committee account",
    which is controlled by the committee members and has a few abilities and restrictions other accounts
    do not).

    All asset IDs are of the form `1.3.x`.  If you were the 29th asset to be registered,
    your asset's ID will be `1.3.29`.  Asset `0` is special (it's ESH, which is considered the "core asset").

    The first and second number together identify the kind of thing you're talking about (`1.2` for accounts,
    `1.3` for assets).  The third number identifies the particular thing.

- How do I get the `network_add_nodes` command to work?  Why is it so complicated?

    You need to follow the instructions in the ["Accessing restrictable node API sets"](#accessing-restrictable-node-api-sets) section to
    allow a username/password access to the `network_node` API set.  Then you need
    to pass the username/password to the `cli_wallet` on the command line.

    It's set up this way so that the default configuration is secure even if the RPC port is
    publicly accessible.  It's fine if your `witness_node` allows the general public to query
    the database or broadcast transactions (in fact, this is how the hosted web UI works).  It's
    less fine if your `witness_node` allows the general public to control which p2p nodes it's
    connecting to.  Therefore the API to add p2p connections needs to be set up with proper access
    controls.


License
-------

Esher Core is under the MIT license. See [LICENSE](https://github.com/esherproject/esher-core/blob/master/LICENSE.txt)
for more information.
