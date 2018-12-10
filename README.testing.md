# Syncwerk Server Tests

## Run it locally

To run the tests, you need to install pytest first:

```sh
pip install -r ci/requirements.txt
```

Compile and install syncwerk-server-ccnet and syncwerk-server
```
cd syncwerk-server-ccnet
make
sudo make install

cd syncwerk-server
make
sudo make install
```

Then run the tests with
```sh
cd syncwerk-server
./run_tests.sh
```

By default the test script would try to start syncwerk-server-ccnet and syncwerk-server-daemon in `/usr/local/bin`, if you `make install` to another location, say `/opt/local`, run it like this:
```sh
SYNCWERK_INSTALL_PREFIX=/opt/local ./run_tests.sh
```
